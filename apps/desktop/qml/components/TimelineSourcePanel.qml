pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia

GlassSurface {
    id: root

    required property var theme
    required property var timelineDocument
    required property var sourceClip
    required property var catalogAssets
    required property bool collapsed
    required property string activeTab
    property bool embeddedDockHeader: false
    property bool workspaceActive: true
    property bool programGangEnabled: false
    property bool programPlaying: false
    property var programGangState: ({})
    signal toggleCollapseRequested()
    signal tabChangeRequested(string tab)
    signal mediaPickerRequested()
    signal importWorkspaceRequested()
    signal batchRelinkRequested()
    signal mediaDragMoved(var payload, real sceneX, real sceneY)
    signal mediaDragDropped(var payload, real sceneX, real sceneY)
    signal mediaDragCanceled()
    signal programGangToggleRequested(bool enabled)
    signal gangedSourceSeekRequested(real sourceSeconds)
    signal gangedPlaybackToggleRequested()

    property string mediaSearchText: ""
    property var mediaSearchScopes: ["all"]
    property string mediaFilmstripDensity: "standard"
    property string mediaViewMode: "grid"
    property real mediaThumbnailTargetSize: 112
    property int mediaFilterRevision: 0
    property int mediaCatalogRevision: 0
    property int mediaFilterCursor: 0
    property int mediaFilterSourceCount: 0
    property int mediaFilterActiveRevision: 0
    property string mediaFilterQuery: ""
    property var mediaFilterScopes: ["all"]
    property string mediaFilterBinId: ""
    property string mediaFilterCollectionId: ""
    property bool mediaFilterBusy: false
    property var filteredMediaAssetIds: ({})
    property int filteredMediaSelectionRevision: 0
    // Read-only file references used by the loaded timeline. Project-only rows
    // share the media browser actions but never enter the catalog's visible range.
    property var projectReferencedMedia: []
    property var projectReferencedMediaById: ({})
    property int projectOnlyMediaCount: 0
    property string projectReferencedMediaFingerprint: ""
    property bool projectReferencedMediaSnapshotDirty: true
    property real mediaGridScrollPosition: 0
    property real mediaListScrollPosition: 0
    property int mediaSelectionAnchorIndex: -1
    property string mediaBinDraft: ""
    property string mediaBinRenameDraft: ""
    property string selectedMediaBinId: ""
    property var selectedMediaAssetIds: []
    property string mediaTagsDraft: ""
    property var takeComparisonAssets: []
    property bool takeComparisonPlaying: false
    property bool sourceFrameAvailable: false
    property var mediaUsageAnalysis: ({"summary": ({}), "items": []})
    property string mediaUsageFilter: "all"
    property string mediaUsageCanonicalAssetId: ""
    property var mediaInspectorAsset: ({})
    property var mediaInspectorTechnical: ({})
    property string mediaInspectorNotesDraft: ""
    property string mediaInspectorCustomFieldsDraft: ""
    property string mediaInspectorApplyStatus: ""
    property string smartCollectionEditingId: ""
    property string smartCollectionNameDraft: ""
    property string smartCollectionMediaTypeDraft: "any"
    property int smartCollectionRatingDraft: 0
    property string smartCollectionLabelDraft: "any"
    property string smartCollectionTagDraft: ""
    property string smartCollectionDateFromDraft: ""
    property string smartCollectionDateToDraft: ""
    property string smartCollectionMinimumDurationDraft: ""
    property string smartCollectionMaximumDurationDraft: ""
    property string smartCollectionOrientationDraft: "any"
    property string smartCollectionOnlineDraft: "any"
    property string smartCollectionUsageDraft: "any"
    property string smartCollectionCustomFieldDraft: ""
    property string smartCollectionCustomValueDraft: ""
    property int mediaMetadataRevision: 0
    property real previewSeconds: 0
    property bool sourceTapeAutoAdvance: false
    property real suspendedSourceSeconds: -1
    property string suspendedSourceUrl: ""
    property bool sourceDecoderPrimed: false
    property bool sourceFreshDecoderPending: false
    property bool sourcePrerollActive: false
    property bool sourcePrerollFirstFrameSeen: false
    property bool sourcePrerollSeekIssued: false
    property bool sourcePrerollSeekAcknowledged: false
    property int sourcePrerollSeekAcknowledgedFrameGeneration: -1
    property real sourcePrerollSeekRequestedPositionMs: -1
    property real sourcePrerollSeekAcknowledgedPositionMs: -1
    property int sourcePrerollNativeReseekCount: 0
    property bool sourcePrerollPlayAfterSeek: false
    property bool sourcePrerollPlaybackHandoffPending: false
    property int sourcePrerollPlaybackHandoffGeneration: -1
    property string sourcePrerollPlaybackHandoffUrl: ""
    property real sourcePrerollPlaybackHandoffTargetSeconds: -1
    property int sourcePrerollPlaybackStartFrameGeneration: -1
    property real sourcePrerollPlaybackStartSeconds: -1
    property bool sourcePrerollFailed: false
    property bool sourceUserPlaybackRequested: false
    property int sourceFrameGeneration: 0
    property int sourcePrerollSeekFrameGeneration: -1
    property int sourcePrerollGeneration: 0
    property int sourcePrerollSeekTimerGeneration: -1
    property int sourcePrerollDeadlineGeneration: -1
    property real sourcePrerollTargetSeconds: 0
    property string sourcePrerollUrl: ""
    property int sourcePrerollCompletedFrameGeneration: -1
    property int sourcePrerollCompletedSeekFrameGeneration: -1
    property int sourcePrerollCompletedAcknowledgedFrameGeneration: -1
    property real sourcePrerollCompletedAcknowledgedPositionMs: -1
    property real sourcePrerollCompletedTargetSeconds: -1
    property real sourcePrerollCompletedPositionSeconds: -1
    property bool sourcePrerollCompletedFrameValid: false
    readonly property bool sourceViewerActive: workspaceActive && visible && !collapsed && activeTab === "source"
    readonly property bool mediaBrowserActive: workspaceActive && visible && !collapsed && activeTab === "media"
    readonly property var documentController: timelineDocument !== undefined && timelineDocument !== null ? timelineDocument : null
    readonly property var sourceState: sourceClip !== undefined && sourceClip !== null ? sourceClip : ({})
    readonly property var assetCatalog: catalogAssets !== undefined && catalogAssets !== null ? catalogAssets : null
    readonly property bool hasSourceClipValue: documentController !== null && documentController.hasSourceClip
    readonly property bool sourceTapeEnabledValue: documentController !== null
                                                    && documentController.sourceTapeEnabled
    readonly property int sourceTapeIndexValue: sourceTapeEnabledValue
                                                  ? Number(documentController.sourceTapeIndex) : -1
    readonly property var sourceTapeItemsValue: sourceTapeEnabledValue
                                                 ? documentController.sourceTapeItems : []
    readonly property bool sourceAnalysisPending: sourceState.mediaAnalysisStatus === "queued"
                                                  || sourceState.mediaAnalysisStatus === "analyzing"
    readonly property bool sourceEditReady: hasSourceClipValue
                                            && documentController !== null
                                            && documentController.sourcePatchReady
    readonly property real sourceInSecondsValue: documentController !== null ? Number(documentController.sourceInSeconds || 0) : 0
    readonly property real sourceOutSecondsValue: documentController !== null ? Number(documentController.sourceOutSeconds || 0) : 0
    readonly property real framesPerSecondValue: documentController !== null ? Number(documentController.framesPerSecond || 30) : 30
    readonly property int assetCountValue: assetCatalog !== null ? Number(assetCatalog.assetCount || 0) : 0
    readonly property int totalAssetCountValue: assetCatalog !== null ? Number(assetCatalog.totalAssetCount || 0) : 0
    readonly property int browsableMediaCountValue: totalAssetCountValue + projectOnlyMediaCount
    readonly property int mediaResultCountValue: filteredMediaModel.count
    readonly property bool mediaFilterResultsPending: mediaFilterBusy
                                                        || mediaFilterRevision !== mediaFilterActiveRevision
    readonly property int selectedMediaVisibleAssetCount: {
        filteredMediaSelectionRevision
        const visibleIds = filteredMediaAssetIds
        const selectedIds = selectedMediaAssetIds
        let count = 0
        for (let index = 0; index < selectedIds.length; ++index) {
            if (visibleIds[String(selectedIds[index])] === true)
                count += 1
        }
        return count
    }
    readonly property int selectedMediaHiddenAssetCount: mediaFilterResultsPending ? 0
                                                         : Math.max(0, selectedMediaAssetIds.length
                                                                     - selectedMediaVisibleAssetCount)
    readonly property var mediaBinsModel: documentController ? documentController.mediaBins : []
    readonly property var selectedMediaAssetValue: selectedMediaAssetIds.length === 1
                                                    ? mediaAssetForId(selectedMediaAssetIds[0]) : ({})
    readonly property string selectedMediaTitleValue: {
        const fileName = String(selectedMediaAssetValue.filePath || "").split(/[\\/]/).pop()
        return fileName.length > 0 ? fileName : String(
            selectedMediaAssetValue.cameraLabel || selectedMediaAssetValue.displayLabel
            || selectedMediaAssetValue.assetId || "Media")
    }
    readonly property bool selectedMediaOnlineValue: selectedMediaAssetValue.online === true
    readonly property int mediaFilmstripSampleCount: mediaFilmstripDensity === "compact" ? 5
                                                       : mediaFilmstripDensity === "detailed" ? 15 : 9
    readonly property string timelineSourcePanelStyleContract: "timeline-source-panel-style:v3|surface:shared|tabs:filter-chip|media-well:semantic|type:shared|spacing:shared|transport:shared|states:empty-disabled-offline-no-results|layout:video-editor-preserved|pool:virtualized-grid-list-size-filter-count-selection-actions"
    readonly property string timelineMediaSelectionVisibilityContract: "timeline-media-selection-visibility:v1|selection:persistent|filtered-model-id-map:true|status:hidden-count|pending:updating-results"
    readonly property string timelineUnifiedMediaSearchContract: "timeline-unified-media-search:v2|one-field:filename-metadata-tags-transcript-captions-markers-on-screen-text|fields:search-menu-composable|filters:bin-plus-smart-collection|quoted-phrases:true|results:matched-scope|default-density:query-contextual"
    readonly property string timelineMediaHoverScrubContract: "timeline-media-hover-scrub:v1|pointer:debounced-80ms|density:compact-5-standard-9-detailed-15|decode:ffmpeg-384x216-single-frame|queue:latest-per-asset-bounded-24|cache:bounded-240-files-96mb|full-resolution-per-move:false|ui:hover-contextual"
    readonly property string timelineTakeComparisonContract: "timeline-take-comparison:v1|selection:2-to-8|view:contact-sheet|playback:synchronized-normalized-drift-correction|audio:leader-only|rating:controller-owned|choice:load-source|ui:modular-contextual"
    readonly property string timelineMediaUsageContract: "timeline-media-usage-ui:v1|categories:used-unused-duplicate-offline-derivative|reveal:timeline-or-catalog|remove:unused-confirmed|consolidate:explicit-canonical-verified-group-confirmed|state:controller-undo-redo-save-reopen|ui:view-menu-modular-filtered"
    readonly property string timelineSourceTapeContract: "timeline-source-tape:v1|scope:active-bin-and-search|reel:continuous-up-to-1000-online-media|identity:per-asset|marks:per-asset-preserved|navigation:previous-next-auto-advance|source:existing-monitor-and-edit-actions|exit:current-source-retained|sequence-mutation:none"
    readonly property string timelineProgramSourceGangContract: "timeline-source-program-gang:v1|direction:program-to-source-and-source-to-program|mapping:clip-trim-rate-and-retime-curve|playback:program-master-source-muted|gap:honest-retain-last-source|match-frame:exact-active-picture|ui:single-direct-link-toggle-and-monitor-disclosure|document-mutation:none"
    readonly property string timelineMediaMetadataInspectorContract: "timeline-media-metadata-inspector:v1|technical:codec-resolution-frame-rate-color-space-audio-channels-camera-date|editable:notes-custom-fields|probe:explicit-contextual|state:undo-redo-save-reopen|ui:single-selection-modular"
    readonly property string timelineSmartCollectionsContract: "timeline-smart-collections:v1|rules:media-type-rating-label-tag-date-duration-orientation-offline-usage-custom-field|logic:all-rules-live|state:create-edit-delete-undo-redo-save-reopen|filter:bin-search-composable|ui:direct-filter-plus-browse-manager"

    readonly property bool hasSource: hasSourceClipValue
                                      && sourceState.mediaType !== undefined

    function optionIndex(options, selectedValue) {
        for (let index = 0; index < options.length; ++index) {
            if (options[index] === selectedValue)
                return index
        }
        return 0
    }

    function sourceEditDisabledReason(actionName) {
        if (!hasSourceClipValue)
            return "Load a clip into Source to " + actionName + "."
        if (sourceAnalysisPending)
            return "Source duration and audio are still being analyzed."
        return "Enable a compatible V or A source patch in the track headers."
    }

    function sourceUsesPlayer() {
        return hasSource
                && (sourceState.mediaType === "video" || sourceState.mediaType === "audio")
    }

    function sourcePreviewImage() {
        if (!hasSource)
            return ""
        if (sourceState.mediaType === "photo")
            return sourceState.posterSource || ""
        if (sourceState.mediaType === "audio")
            return (sourceState.waveformSource && sourceState.waveformSource.length > 0) ? sourceState.waveformSource : (sourceState.posterSource || "")
        if (sourceState.mediaType === "video")
            return sourceState.posterSource || ""
        return ""
    }

    radius: theme.radiusPanel
    accent: theme.accentWarm400
    accentRail: false
    elevated: true
    edgeOpacity: 0

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
        const fps = Math.max(1, Math.round(framesPerSecondValue))
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

    function restoreSourceAudioOutput() {
        sourcePreviewPlayer.audioOutput = sourcePreviewAudio
    }

    function clearSourcePrerollPlaybackHandoff() {
        sourcePrerollPlayAfterSeek = false
        sourcePrerollPlaybackHandoffPending = false
        sourcePrerollPlaybackHandoffGeneration = -1
        sourcePrerollPlaybackHandoffUrl = ""
        sourcePrerollPlaybackHandoffTargetSeconds = -1
    }

    function finishSourcePrerollPlaybackHandoff(generation, playbackState) {
        const observedPlaybackState = playbackState === undefined
                ? sourcePreviewPlayer.playbackState : Number(playbackState)
        if (!sourcePrerollPlaybackHandoffPending
                || Number(generation) !== sourcePrerollPlaybackHandoffGeneration
                || observedPlaybackState === MediaPlayer.PlayingState
                || sourcePreviewPlayer.playbackState === MediaPlayer.PlayingState)
            return false

        const expectedUrl = sourcePrerollPlaybackHandoffUrl
        const targetSeconds = sourcePrerollPlaybackHandoffTargetSeconds
        const canResume = sourceViewerActive && !programPlaying && !programGangEnabled
                && hasSource && sourceState.mediaType === "video"
                && expectedUrl.length > 0
                && String(sourcePreviewPlayer.source) === expectedUrl
                && String(sourceState.sourceUrl || "") === expectedUrl
                && sourceDecoderPrimed && !sourcePrerollActive
                && Math.abs(sourcePreviewPlayer.position / 1000 - targetSeconds) <= 0.1

        clearSourcePrerollPlaybackHandoff()
        if (!canResume) {
            sourceUserPlaybackRequested = false
            sourceTapeAutoAdvance = false
            return false
        }

        sourceUserPlaybackRequested = true
        sourceTapeAutoAdvance = sourceTapeEnabledValue
        sourcePrerollPlaybackStartFrameGeneration = sourceFrameGeneration
        sourcePrerollPlaybackStartSeconds = sourcePreviewPlayer.position / 1000
        restoreSourceAudioOutput()
        sourcePreviewPlayer.play()
        return true
    }

    function requestSourcePlaybackAfterFreshPreroll() {
        if (!sourceFreshDecoderPending || sourceDecoderPrimed || !sourceViewerActive
                || sourceState.mediaType !== "video" || programPlaying || programGangEnabled)
            return false
        sourcePrerollFailed = false
        sourceTapeAutoAdvance = false
        if (!beginSourcePreroll(previewSeconds))
            return false
        sourcePrerollPlayAfterSeek = true
        sourceUserPlaybackRequested = true
        return true
    }

    function cancelSourcePreroll(pausePlayer) {
        sourcePrerollGeneration += 1
        sourcePrerollDeadlineTimer.stop()
        sourcePrerollSeekTimer.stop()
        sourcePrerollDeadlineGeneration = -1
        sourcePrerollSeekTimerGeneration = -1
        const wasActive = sourcePrerollActive
        sourcePrerollActive = false
        clearSourcePrerollPlaybackHandoff()
        sourcePrerollFirstFrameSeen = false
        sourcePrerollSeekIssued = false
        sourcePrerollSeekFrameGeneration = -1
        sourcePrerollSeekAcknowledged = false
        sourcePrerollSeekAcknowledgedFrameGeneration = -1
        sourcePrerollSeekRequestedPositionMs = -1
        sourcePrerollSeekAcknowledgedPositionMs = -1
        sourcePrerollNativeReseekCount = 0
        sourcePrerollUrl = ""
        if (pausePlayer === true && wasActive
                && sourcePreviewPlayer.playbackState === MediaPlayer.PlayingState)
            sourcePreviewPlayer.pause()
    }

    function sourcePrerollIsCurrent(generation) {
        const expectedGeneration = generation === undefined
                ? sourcePrerollGeneration : Number(generation)
        return expectedGeneration === sourcePrerollGeneration
                && sourcePrerollActive && sourceViewerActive && !programPlaying
                && !programGangEnabled && !sourceTapeAutoAdvance
                && hasSource && sourceState.mediaType === "video"
                && sourcePrerollUrl.length > 0
                && String(sourcePreviewPlayer.source) === sourcePrerollUrl
                && String(sourceState.sourceUrl || "") === sourcePrerollUrl
    }

    function maybeStartSourcePrerollPlayback() {
        if (!sourcePrerollIsCurrent())
            return false
        const durationMs = Number(sourcePreviewPlayer.duration)
        if (sourcePreviewPlayer.mediaStatus !== MediaPlayer.LoadedMedia
                || !sourcePreviewPlayer.seekable || !Number.isFinite(durationMs)
                || durationMs <= 0)
            return false
        const frameStepMs = 1000 / Math.max(1, framesPerSecondValue)
        sourcePrerollTargetSeconds = Math.min(
                    sourcePrerollTargetSeconds,
                    Math.max(0, (durationMs - frameStepMs) / 1000))
        if (durationMs <= sourcePrerollTargetSeconds * 1000)
            return false
        sourcePreviewPlayer.audioOutput = null
        sourceTapeAutoAdvance = false
        if (sourcePreviewPlayer.playbackState !== MediaPlayer.PlayingState)
            sourcePreviewPlayer.play()
        return true
    }

    function beginSourcePreroll(seconds) {
        if (!sourceFreshDecoderPending || sourceDecoderPrimed || !sourceViewerActive
                || programPlaying || programGangEnabled || sourceTapeAutoAdvance || !hasSource
                || sourceUserPlaybackRequested || sourceState.mediaType !== "video")
            return false
        const sourceUrl = String(sourcePreviewPlayer.source)
        if (sourceUrl.length === 0 || sourceUrl !== String(sourceState.sourceUrl || ""))
            return false
        if (!sourcePrerollActive) {
            sourcePrerollGeneration += 1
            sourcePrerollActive = true
            sourcePrerollUrl = sourceUrl
            sourcePrerollSeekTimerGeneration = sourcePrerollGeneration
            sourcePrerollDeadlineGeneration = sourcePrerollGeneration
            sourcePrerollFirstFrameSeen = false
            sourcePrerollSeekIssued = false
            sourcePrerollSeekFrameGeneration = -1
            sourcePrerollSeekAcknowledged = false
            sourcePrerollSeekAcknowledgedFrameGeneration = -1
            sourcePrerollSeekRequestedPositionMs = -1
            sourcePrerollSeekAcknowledgedPositionMs = -1
            sourcePrerollNativeReseekCount = 0
            clearSourcePrerollPlaybackHandoff()
            sourcePrerollFailed = false
            sourcePrerollCompletedFrameGeneration = -1
            sourcePrerollCompletedSeekFrameGeneration = -1
            sourcePrerollCompletedAcknowledgedFrameGeneration = -1
            sourcePrerollCompletedAcknowledgedPositionMs = -1
            sourcePrerollCompletedTargetSeconds = -1
            sourcePrerollCompletedPositionSeconds = -1
            sourcePrerollCompletedFrameValid = false
            sourcePrerollPlaybackStartFrameGeneration = -1
            sourcePrerollPlaybackStartSeconds = -1
            sourcePreviewPlayer.audioOutput = null
            sourceTapeAutoAdvance = false
            sourcePrerollDeadlineTimer.restart()
        }
        sourcePrerollTargetSeconds = Math.max(
                    0, Math.min(Number(sourceState.durationSeconds || 0), Number(seconds || 0)))
        sourcePrerollSeekIssued = false
        if (sourcePrerollFirstFrameSeen)
            sourcePrerollSeekTimer.restart()
        maybeStartSourcePrerollPlayback()
        return true
    }

    function issueSourcePrerollSeek(generation) {
        if (!sourcePrerollIsCurrent(generation) || !sourcePrerollFirstFrameSeen
                || !sourcePreviewPlayer.seekable)
            return
        const durationMs = Number(sourcePreviewPlayer.duration)
        const frameStepMs = 1000 / Math.max(1, framesPerSecondValue)
        if (!Number.isFinite(durationMs) || durationMs <= 0)
            return
        const targetMs = Math.max(
                    0, Math.min(Math.round(sourcePrerollTargetSeconds * 1000),
                                Math.floor(durationMs - frameStepMs)))
        sourcePrerollTargetSeconds = targetMs / 1000
        sourcePrerollSeekFrameGeneration = sourceFrameGeneration
        sourcePrerollSeekAcknowledged = false
        sourcePrerollSeekAcknowledgedFrameGeneration = -1
        sourcePrerollSeekRequestedPositionMs = targetMs
        sourcePrerollSeekAcknowledgedPositionMs = -1
        sourcePrerollSeekIssued = true
        sourcePreviewPlayer.position = targetMs
    }

    function retrySourcePrerollSeekAfterOvershoot(generation) {
        if (!sourcePrerollIsCurrent(generation))
            return false
        if (sourcePrerollNativeReseekCount >= 1) {
            failSourcePreroll(sourcePrerollGeneration)
            return false
        }
        sourcePrerollNativeReseekCount += 1
        sourcePrerollSeekIssued = false
        sourcePrerollSeekAcknowledged = false
        sourcePrerollSeekAcknowledgedFrameGeneration = -1
        sourcePrerollSeekAcknowledgedPositionMs = -1
        sourcePrerollSeekTimer.restart()
        return true
    }

    function completeSourcePreroll() {
        if (!sourcePrerollIsCurrent())
            return
        const completedTargetSeconds = sourcePrerollTargetSeconds
        const completedSourceUrl = sourcePrerollUrl
        const playAfterSeek = sourcePrerollPlayAfterSeek
        const completedSeekFrameGeneration = sourcePrerollSeekFrameGeneration
        const completedAcknowledgedFrameGeneration =
            sourcePrerollSeekAcknowledgedFrameGeneration
        const completedAcknowledgedPositionMs = sourcePrerollSeekAcknowledgedPositionMs
        const completedFrameGeneration = sourceFrameGeneration
        const completedFrameValid = sourceFrameAvailable
        sourcePrerollDeadlineTimer.stop()
        sourcePrerollSeekTimer.stop()
        sourcePrerollActive = false
        sourcePrerollFirstFrameSeen = false
        sourcePrerollSeekIssued = false
        sourcePrerollSeekFrameGeneration = -1
        sourcePrerollSeekAcknowledged = false
        sourcePrerollSeekAcknowledgedFrameGeneration = -1
        sourcePrerollSeekRequestedPositionMs = -1
        sourcePrerollSeekAcknowledgedPositionMs = -1
        sourcePrerollNativeReseekCount = 0
        sourcePrerollUrl = ""
        sourcePrerollPlayAfterSeek = false
        sourcePrerollPlaybackHandoffPending = playAfterSeek
        sourcePrerollPlaybackHandoffGeneration = playAfterSeek ? sourcePrerollGeneration : -1
        sourcePrerollPlaybackHandoffUrl = playAfterSeek ? completedSourceUrl : ""
        sourcePrerollPlaybackHandoffTargetSeconds = playAfterSeek ? completedTargetSeconds : -1
        sourcePrerollFailed = false
        sourceDecoderPrimed = true
        sourceFreshDecoderPending = false
        sourceUserPlaybackRequested = playAfterSeek
        sourceTapeAutoAdvance = false
        sourcePreviewPlayer.pause()
        previewSeconds = sourcePreviewPlayer.position / 1000
        sourcePrerollCompletedTargetSeconds = completedTargetSeconds
        sourcePrerollCompletedSeekFrameGeneration = completedSeekFrameGeneration
        sourcePrerollCompletedFrameGeneration = completedFrameGeneration
        sourcePrerollCompletedPositionSeconds = sourcePreviewPlayer.position / 1000
        sourcePrerollCompletedFrameValid = completedFrameValid
        sourcePrerollCompletedAcknowledgedFrameGeneration =
            completedAcknowledgedFrameGeneration
        sourcePrerollCompletedAcknowledgedPositionMs = completedAcknowledgedPositionMs
        if (playAfterSeek && sourcePreviewPlayer.playbackState !== MediaPlayer.PlayingState)
            finishSourcePrerollPlaybackHandoff(sourcePrerollPlaybackHandoffGeneration)
    }

    function failSourcePreroll(generation) {
        if (!sourcePrerollIsCurrent(generation))
            return
        const target = sourcePrerollTargetSeconds
        sourcePrerollGeneration += 1
        sourcePrerollDeadlineGeneration = -1
        sourcePrerollSeekTimerGeneration = -1
        sourcePrerollActive = false
        clearSourcePrerollPlaybackHandoff()
        sourcePrerollFirstFrameSeen = false
        sourcePrerollSeekIssued = false
        sourcePrerollSeekFrameGeneration = -1
        sourcePrerollSeekAcknowledged = false
        sourcePrerollSeekAcknowledgedFrameGeneration = -1
        sourcePrerollSeekRequestedPositionMs = -1
        sourcePrerollSeekAcknowledgedPositionMs = -1
        sourcePrerollNativeReseekCount = 0
        sourcePrerollUrl = ""
        sourcePrerollFailed = true
        sourceTapeAutoAdvance = false
        sourceUserPlaybackRequested = false
        sourcePreviewPlayer.pause()
        sourceVideoMonitor.clearOutput()
        sourceFrameAvailable = false
        previewSeconds = target
    }

    function handleSourceVideoFrame() {
        sourceFrameGeneration += 1
        const size = sourceVideoMonitor.videoSink.videoSize
        if (!sourceViewerActive) {
            sourceFrameAvailable = false
            if (size.width > 0 && size.height > 0)
                sourceVideoMonitor.clearOutput()
            return
        }
        sourceFrameAvailable = size.width > 0 && size.height > 0
        if (!sourceFrameAvailable)
            return
        if (!sourcePrerollActive) {
            if (sourceFreshDecoderPending
                    && sourcePreviewPlayer.playbackState === MediaPlayer.PlayingState) {
                sourceDecoderPrimed = true
                sourceFreshDecoderPending = false
                sourceUserPlaybackRequested = false
            }
            return
        }
        if (!sourcePrerollIsCurrent()) {
            cancelSourcePreroll(true)
            return
        }
        if (!sourcePrerollFirstFrameSeen) {
            sourcePrerollFirstFrameSeen = true
            sourcePrerollSeekFrameGeneration = sourceFrameGeneration
            sourcePrerollSeekIssued = false
            sourcePrerollSeekTimer.restart()
            return
        }
        if (!sourcePrerollSeekIssued
                || sourceFrameGeneration <= sourcePrerollSeekFrameGeneration)
            return
        const nativePositionMs = Number(sourcePreviewPlayer.position)
        const requestedPositionMs = sourcePrerollSeekRequestedPositionMs
        if (!Number.isFinite(nativePositionMs) || requestedPositionMs < 0)
            return
        if (nativePositionMs - requestedPositionMs > 100) {
            retrySourcePrerollSeekAfterOvershoot(sourcePrerollGeneration)
            return
        }
        if (!sourcePrerollSeekAcknowledged) {
            if (nativePositionMs <= requestedPositionMs)
                return
            sourcePrerollSeekAcknowledged = true
            sourcePrerollSeekAcknowledgedFrameGeneration = sourceFrameGeneration
            sourcePrerollSeekAcknowledgedPositionMs = nativePositionMs
            return
        }
        if (sourceFrameGeneration <= sourcePrerollSeekAcknowledgedFrameGeneration ||
            nativePositionMs <= requestedPositionMs)
            return
        const displayedSeconds = nativePositionMs / 1000
        if (Math.abs(displayedSeconds - sourcePrerollTargetSeconds) <= 0.1)
            completeSourcePreroll()
    }

    function seekSource(seconds, driveProgram) {
        if (!hasSourceClipValue)
            return
        const bounded = Math.max(0, Math.min(Number(sourceState.durationSeconds || 0), Number(seconds || 0)))
        previewSeconds = bounded
        if (sourceViewerActive && sourcePrerollActive) {
            sourcePrerollTargetSeconds = bounded
            sourcePrerollSeekIssued = false
            sourcePrerollSeekAcknowledged = false
            sourcePrerollSeekAcknowledgedFrameGeneration = -1
            sourcePrerollSeekRequestedPositionMs = -1
            sourcePrerollSeekAcknowledgedPositionMs = -1
            sourcePrerollNativeReseekCount = 0
            if (sourcePrerollFirstFrameSeen)
                sourcePrerollSeekTimer.restart()
        } else if (sourceViewerActive && sourceFreshDecoderPending
                   && !sourceDecoderPrimed && !sourceUserPlaybackRequested
                   && sourceState.mediaType === "video" && !programGangEnabled
                   && !sourceTapeAutoAdvance) {
            beginSourcePreroll(bounded)
        } else if (sourceViewerActive) {
            sourcePreviewPlayer.position = Math.round(bounded * 1000)
        } else {
            suspendedSourceSeconds = bounded
            suspendedSourceUrl = String(sourceState.sourceUrl || "")
        }
        if (programGangEnabled && driveProgram === true)
            root.gangedSourceSeekRequested(bounded)
    }

    function loadIntoSource(assetId, filePath, displayLabel, thumbnailSource) {
        if (documentController === null)
            return
        documentController.loadSourceClipFromGallery(assetId, filePath, displayLabel, 0, thumbnailSource)
        tabChangeRequested("source")
    }

    function insertSelectedMediaAtPlayhead() {
        if (!documentController || selectedMediaAssetIds.length !== 1
                || !selectedMediaOnlineValue)
            return
        const asset = selectedMediaAssetValue
        const assetId = String(asset.assetId || "")
        const filePath = String(asset.filePath || "")
        if (assetId.length === 0 || filePath.length === 0)
            return
        documentController.insertClipFromGallery(
                    assetId, filePath, selectedMediaTitleValue, 0, 0, 0,
                    String(asset.thumbnailSource || asset.posterSource || ""))
    }

    function previewSelectedMediaInSource() {
        if (!documentController || selectedMediaAssetIds.length !== 1
                || !selectedMediaOnlineValue)
            return
        const asset = selectedMediaAssetValue
        loadIntoSource(String(asset.assetId || ""), String(asset.filePath || ""),
                       selectedMediaTitleValue,
                       String(asset.thumbnailSource || asset.posterSource || ""))
    }

    function mediaBinName(binId) {
        if (!binId || !documentController)
            return "All Media"
        const bins = mediaBinsModel || []
        for (let index = 0; index < bins.length; ++index) {
            if (String(bins[index].binId) === String(binId))
                return String(bins[index].name)
        }
        return "All Media"
    }

    function mediaPoolScopeLabel() {
        if (!documentController)
            return "All Media"
        const binId = String(documentController.activeMediaBinId || "")
        const collectionId = String(documentController.activeSmartCollectionId || "")
        if (binId.length > 0 && collectionId.length > 0)
            return mediaBinName(binId) + " · " + smartCollectionName(collectionId)
        if (collectionId.length > 0)
            return "Collection · " + smartCollectionName(collectionId)
        return binId.length > 0 ? "Bin · " + mediaBinName(binId) : "All Media"
    }

    function mediaPoolFilterMenuItems() {
        const items = [{
            "key": "scope:all",
            "label": "All Media",
            "icon": "folder",
            "checked": !documentController
                       || (String(documentController.activeMediaBinId || "").length === 0
                           && String(documentController.activeSmartCollectionId || "").length === 0),
            "tooltip": "Clear both filters and show media across the whole library."
        }]
        const bins = mediaBinsModel || []
        for (let index = 0; index < bins.length; ++index) {
            const bin = bins[index] || ({})
            const binId = String(bin.binId || "")
            if (binId.length === 0)
                continue
            const depth = Math.max(0, Math.min(4, Number(bin.depth || 0)))
            items.push({
                "key": "scope:bin:" + binId,
                "label": "Bin · " + "  ".repeat(depth) + String(bin.name || "Bin"),
                "icon": "folder",
                "separatorBefore": index === 0,
                "checked": documentController
                           && String(documentController.activeMediaBinId || "") === binId
            })
        }
        const collections = documentController ? documentController.smartCollections : []
        for (let index = 0; index < collections.length; ++index) {
            const collection = collections[index] || ({})
            const collectionId = String(collection.collectionId || "")
            if (collectionId.length === 0)
                continue
            items.push({
                "key": "scope:collection:" + collectionId,
                "label": "Collection · " + String(collection.name || "Smart Collection"),
                "icon": "filter",
                "separatorBefore": index === 0,
                "checked": String(documentController.activeSmartCollectionId || "") === collectionId
            })
        }
        return items
    }

    function applyMediaPoolFilter(key) {
        if (!documentController)
            return
        const normalized = String(key || "")
        if (normalized === "scope:all") {
            documentController.activeMediaBinId = ""
            documentController.activeSmartCollectionId = ""
        } else if (normalized.indexOf("scope:bin:") === 0) {
            const binId = normalized.slice(10)
            documentController.activeMediaBinId =
                    String(documentController.activeMediaBinId || "") === binId ? "" : binId
        } else if (normalized.indexOf("scope:collection:") === 0) {
            const collectionId = normalized.slice(17)
            documentController.activeSmartCollectionId =
                    String(documentController.activeSmartCollectionId || "") === collectionId
                    ? "" : collectionId
        }
        scheduleMediaFilterRefresh()
    }

    function mediaSearchScopeMenuItems() {
        const scopes = [
            { "key": "all", "label": "All fields", "icon": "search" },
            { "key": "filename", "label": "Filename", "icon": "media" },
            { "key": "metadata", "label": "Metadata", "icon": "info" },
            { "key": "tags", "label": "Tags", "icon": "filter" },
            { "key": "transcript", "label": "Transcript", "icon": "audio" },
            { "key": "captions", "label": "Captions", "icon": "text" },
            { "key": "markers", "label": "Markers", "icon": "mark-in" },
            { "key": "text", "label": "On-screen text", "icon": "text" }
        ]
        for (let index = 0; index < scopes.length; ++index)
            scopes[index].checked = mediaSearchScopes.indexOf(scopes[index].key) >= 0
        return scopes
    }

    function mediaPoolBrowseMenuItems() {
        return [
            { "key": "density:compact", "label": "Compact previews · 5 frames",
              "checked": mediaFilmstripDensity === "compact", "icon": "density" },
            { "key": "density:standard", "label": "Standard previews · 9 frames",
              "checked": mediaFilmstripDensity === "standard", "icon": "density" },
            { "key": "density:detailed", "label": "Detailed previews · 15 frames",
              "checked": mediaFilmstripDensity === "detailed", "icon": "density" },
            { "key": "manage-bins", "label": "Manage Bins…", "icon": "folder",
              "separatorBefore": true },
            { "key": "smart-collections", "label": "Manage Smart Collections…", "icon": "filter" },
            { "key": "source-tape", "label": "Review as Source Tape", "icon": "play",
              "separatorBefore": true,
              "enabled": assetCountValue > 0,
              "disabledReason": "Import media before starting a source tape." },
            { "key": "usage", "label": "Analyze Media Usage", "icon": "info",
              "enabled": assetCountValue > 0,
              "disabledReason": "Import media before analyzing usage." }
        ]
    }

    function handleMediaPoolBrowseAction(key) {
        const normalized = String(key || "")
        if (normalized.indexOf("density:") === 0) {
            mediaFilmstripDensity = normalized.slice(8)
        } else if (normalized === "manage-bins") {
            mediaBinsPopup.open()
        } else if (normalized === "smart-collections") {
            resetSmartCollectionDraft()
            smartCollectionsPopup.open()
        } else if (normalized === "source-tape") {
            openSourceTape()
        } else if (normalized === "usage") {
            openMediaUsageAnalysis()
        }
    }

    function createMediaBin(parentBinId) {
        if (!documentController)
            return
        const created = documentController.createMediaBin(
                    mediaBinDraft.length > 0 ? mediaBinDraft : "New Bin",
                    parentBinId || "")
        if (created && created.length > 0) {
            selectedMediaBinId = created
            mediaBinRenameDraft = mediaBinName(created)
            mediaBinDraft = ""
        }
    }

    function mediaMetadataFor(assetId) {
        mediaMetadataRevision
        return documentController ? documentController.mediaAssetMetadata(assetId) : ({})
    }

    function catalogAssetForId(assetId) {
        if (!assetCatalog)
            return ({})
        return assetCatalog.assetById(String(assetId || "")) || ({})
    }

    function isCatalogBackedAsset(assetId) {
        const catalogAsset = catalogAssetForId(assetId)
        if (String(catalogAsset.assetId || "").length === 0)
            return false
        const projectAsset = projectAssetForId(assetId)
        const projectPath = String(projectAsset.filePath || "")
        return projectPath.length === 0
                || projectPath === String(catalogAsset.filePath || "")
    }

    function selectionIsCatalogBacked() {
        if (selectedMediaAssetIds.length === 0)
            return false
        for (let index = 0; index < selectedMediaAssetIds.length; ++index) {
            if (!isCatalogBackedAsset(selectedMediaAssetIds[index]))
                return false
        }
        return true
    }

    function projectAssetForId(assetId) {
        return projectReferencedMediaById[String(assetId || "")] || ({})
    }

    function mediaAssetForId(assetId) {
        const catalogAsset = catalogAssetForId(assetId)
        const projectAsset = projectAssetForId(assetId)
        if (String(projectAsset.filePath || "").length > 0) {
            const media = Object.assign({}, projectAsset, catalogAsset || ({}))
            const sourcePathDiffers = String(projectAsset.filePath)
                    !== String(catalogAsset.filePath || "")
            media.assetId = String(assetId || "")
            media.filePath = projectAsset.filePath
            media.mediaType = String(projectAsset.mediaType || catalogAsset.mediaType || "")
            media.online = projectAsset.online !== false
            media.thumbnailSource = String(projectAsset.thumbnailSource || projectAsset.posterSource
                                           || (sourcePathDiffers ? ""
                                               : catalogAsset.thumbnailSource
                                                 || catalogAsset.posterSource || ""))
            media.posterSource = String(projectAsset.posterSource
                                         || (sourcePathDiffers ? ""
                                             : catalogAsset.posterSource || ""))
            if (Number(projectAsset.durationSeconds) > 0)
                media.durationSeconds = Number(projectAsset.durationSeconds)
            if (String(projectAsset.durationText || "").length > 0)
                media.durationText = String(projectAsset.durationText)
            return media
        }
        if (String(catalogAsset.assetId || "").length > 0)
            return catalogAsset
        return projectAsset
    }

    function canRefreshProjectReferencedMedia() {
        return root.visible && root.workspaceActive && !root.collapsed
                && root.activeTab === "media"
    }

    function requestProjectReferencedMediaRefresh() {
        projectReferencedMediaSnapshotDirty = true
        if (canRefreshProjectReferencedMedia())
            projectMediaRefreshTimer.restart()
    }

    function refreshProjectReferencedMedia() {
        if (!canRefreshProjectReferencedMedia()) {
            projectReferencedMediaSnapshotDirty = true
            return false
        }

        let sourceRows = projectReferencedMedia
        const controller = documentController
        if (controller && typeof controller.snapshotTimelineUsedMedia === "function") {
            const snapshot = controller.snapshotTimelineUsedMedia()
            sourceRows = snapshot && typeof snapshot.length === "number" ? snapshot : []
        } else if (!controller) {
            sourceRows = []
        }

        const normalized = []
        const byId = ({})
        const seen = ({})
        for (let index = 0; index < sourceRows.length; ++index) {
            const source = sourceRows[index] || ({})
            const assetId = String(source.assetId || source.mediaId || "").trim()
            const filePath = String(source.filePath || "")
            const mediaType = String(source.mediaType || "")
            if (assetId.length === 0 || filePath.length === 0
                    || (mediaType !== "photo" && mediaType !== "video" && mediaType !== "audio")
                    || seen[assetId] === true)
                continue
            seen[assetId] = true
            const media = Object.assign({}, source)
            media.assetId = assetId
            media.filePath = filePath
            media.mediaType = mediaType
            media.timelineReferenced = true
            if (media.online !== false)
                media.online = true
            if (!String(media.displayLabel || "").trim())
                media.displayLabel = String(media.cameraLabel || media.title
                                            || filePath.split(/[\\/]/).pop())
            byId[assetId] = media
            normalized.push(media)
        }

        const fingerprint = JSON.stringify(normalized)
        projectReferencedMediaSnapshotDirty = false
        if (fingerprint === projectReferencedMediaFingerprint)
            return false

        projectReferencedMediaFingerprint = fingerprint
        projectReferencedMedia = normalized
        projectReferencedMediaById = byId
        recountProjectOnlyMedia()
        scheduleMediaFilterRefresh()
        return true
    }

    function recountProjectOnlyMedia() {
        let count = 0
        for (let index = 0; index < projectReferencedMedia.length; ++index) {
            const assetId = String(projectReferencedMedia[index].assetId || "")
            if (String(catalogAssetForId(assetId).assetId || "").length === 0)
                count += 1
        }
        projectOnlyMediaCount = count
    }

    function mediaSelectionSummary() {
        const selectedCount = selectedMediaAssetIds.length
        if (selectedCount === 0)
            return ""
        if (mediaFilterResultsPending)
            return String(selectedCount) + " selected  ·  updating results"
        const hiddenCount = selectedMediaHiddenAssetCount
        if (hiddenCount > 0)
            return String(selectedCount) + " selected  ·  " + String(hiddenCount) + " hidden"
        if (selectedCount === 1)
            return selectedMediaTitleValue + "  ·  "
                    + String(selectedMediaAssetValue.mediaType || "Media")
        return String(selectedCount) + " selected  ·  choose one to preview or add"
    }

    function scheduleMediaFilterRefresh() {
        mediaFilterRevision += 1
        if (mediaBrowserActive)
            mediaFilterRefreshTimer.restart()
    }

    function refreshFilteredMediaModel() {
        if (!mediaBrowserActive)
            return
        mediaSelectionAnchorIndex = -1
        mediaFilterActiveRevision = mediaFilterRevision
        mediaFilterCursor = 0
        mediaFilterSourceCount = assetCatalog ? Math.max(0, Number(assetCatalog.assetCount || 0)) : 0
        mediaFilterQuery = mediaSearchText.trim()
        mediaFilterScopes = mediaSearchScopes.slice(0)
        mediaFilterBinId = documentController
                ? String(documentController.activeMediaBinId || "") : ""
        mediaFilterCollectionId = documentController
                ? String(documentController.activeSmartCollectionId || "") : ""
        filteredMediaModel.clear()
        filteredMediaAssetIds = ({})
        filteredMediaSelectionRevision += 1
        if (assetCatalog && typeof assetCatalog.setVisibleAssetRange === "function")
            assetCatalog.setVisibleAssetRange(-1, -1)
        if (mediaFilterSourceCount + projectReferencedMedia.length === 0) {
            mediaFilterBusy = false
            refreshVisibleMediaRange()
            return
        }
        mediaFilterBusy = true
        mediaFilterBatchTimer.restart()
    }

    function appendFilteredMediaBatch() {
        if (!mediaBrowserActive)
            return
        if (mediaFilterActiveRevision !== mediaFilterRevision) {
            mediaFilterBusy = false
            return
        }
        const totalSourceCount = mediaFilterSourceCount + projectReferencedMedia.length
        const batchEnd = Math.min(totalSourceCount, mediaFilterCursor + 72)
        const query = mediaFilterQuery
        const lowerQuery = query.toLocaleLowerCase()
        for (let index = mediaFilterCursor; index < batchEnd; ++index) {
            const isCatalogAsset = index < mediaFilterSourceCount
            const asset = isCatalogAsset
                    ? assetCatalog.assetAt(index)
                    : projectReferencedMedia[index - mediaFilterSourceCount]
            const assetId = String(asset.assetId || "")
            const mediaType = String(asset.mediaType || "")
            if (assetId.length === 0
                    || (mediaType !== "video" && mediaType !== "photo" && mediaType !== "audio"))
                continue
            // Keep the first matching catalog row if it passed these filters;
            // project refs supply IDs absent from the current catalog results.
            if (!isCatalogAsset && filteredMediaAssetIds[assetId] === true)
                continue
            if (documentController
                    && !documentController.mediaAssetVisibleInBin(assetId, mediaFilterBinId))
                continue
            if (documentController
                    && !documentController.mediaAssetMatchesSmartCollection(
                        asset, mediaFilterCollectionId))
                continue
            if (query.length > 0 && documentController
                    && documentController.searchMediaAsset(
                        assetId, query, mediaFilterScopes, asset).matches !== true)
                continue
            if (query.length > 0 && !documentController) {
                const filename = String(asset.filePath || "").split(/[\\/]/).pop()
                if (filename.toLocaleLowerCase().indexOf(lowerQuery) < 0)
                    continue
            }
            filteredMediaModel.append({
                "assetId": assetId,
                "assetIndex": isCatalogAsset ? index : -1,
                "filteredIndex": filteredMediaModel.count
            })
            filteredMediaAssetIds[assetId] = true
        }
        filteredMediaSelectionRevision += 1
        mediaFilterCursor = batchEnd
        if (batchEnd < totalSourceCount) {
            mediaFilterBatchTimer.restart()
            return
        }
        mediaFilterBusy = false
        refreshVisibleMediaRange()
    }

    function resetMediaFilters() {
        mediaSearchText = ""
        mediaSearchScopes = ["all"]
        if (documentController) {
            documentController.activeMediaBinId = ""
            documentController.activeSmartCollectionId = ""
        }
        scheduleMediaFilterRefresh()
    }

    function refreshVisibleMediaRange() {
        if (!mediaBrowserActive)
            return
        if (!assetCatalog || typeof assetCatalog.setVisibleAssetRange !== "function")
            return
        const view = mediaViewLoader.item
        if (!view || !root.visible || root.collapsed || root.activeTab !== "media"
                || !view.visible || view.width <= 0 || view.height <= 0
                || filteredMediaModel.count === 0) {
            assetCatalog.setVisibleAssetRange(-1, -1)
            return
        }

        // Sample every visible grid column and the viewport top, middle, and
        // bottom. Empty trailing cells must not hide earlier columns from the
        // visible range. Convert filtered rows back to catalog indices.
        const xs = []
        if (mediaViewMode === "grid" && Number(view.cellWidth || 0) > 0) {
            const columns = Math.max(1, Math.ceil(view.width / view.cellWidth))
            for (let column = 0; column < columns; ++column)
                xs.push(Math.min(view.width - 1, column * view.cellWidth
                                 + Math.min(theme.space8, view.cellWidth / 2)))
        } else {
            xs.push(1, Math.max(1, view.width / 2), Math.max(1, view.width - 2))
        }
        const ys = [1, Math.max(1, view.height / 2), Math.max(1, view.height - 2)]
        let firstCatalogAsset = Number.POSITIVE_INFINITY
        let lastCatalogAsset = -1
        for (let xIndex = 0; xIndex < xs.length; ++xIndex) {
            for (let yIndex = 0; yIndex < ys.length; ++yIndex) {
                const filteredIndex = view.indexAt(xs[xIndex], ys[yIndex])
                if (filteredIndex >= 0) {
                    const entry = filteredMediaModel.get(filteredIndex)
                    if (entry && Number(entry.assetIndex) >= 0) {
                        firstCatalogAsset = Math.min(firstCatalogAsset, Number(entry.assetIndex))
                        lastCatalogAsset = Math.max(lastCatalogAsset, Number(entry.assetIndex))
                    }
                }
            }
        }
        if (lastCatalogAsset < 0 || !Number.isFinite(firstCatalogAsset)) {
            assetCatalog.setVisibleAssetRange(-1, -1)
            return
        }
        assetCatalog.setVisibleAssetRange(firstCatalogAsset, lastCatalogAsset)
    }

    function customFieldsText(fields) {
        const keys = []
        for (const key in (fields || ({})))
            keys.push(key)
        keys.sort(function(first, second) {
            return String(first).localeCompare(String(second))
        })
        const lines = []
        for (let index = 0; index < keys.length; ++index)
            lines.push(keys[index] + ": " + String(fields[keys[index]] || ""))
        return lines.join("\n")
    }

    function parseCustomFields(text) {
        const fields = ({})
        const lines = String(text || "").split(/\r?\n/)
        for (let index = 0; index < lines.length; ++index) {
            const separator = lines[index].indexOf(":")
            if (separator <= 0)
                continue
            const key = lines[index].slice(0, separator).trim()
            const value = lines[index].slice(separator + 1).trim()
            if (key.length > 0 && value.length > 0)
                fields[key] = value
        }
        return fields
    }

    function openMediaMetadataInspector() {
        if (!documentController || selectedMediaAssetIds.length !== 1)
            return
        const asset = catalogAssetForId(selectedMediaAssetIds[0])
        const editorial = mediaMetadataFor(selectedMediaAssetIds[0])
        mediaInspectorAsset = asset
        mediaInspectorNotesDraft = String(editorial.notes || "")
        mediaInspectorCustomFieldsDraft = customFieldsText(editorial.customFields || ({}))
        mediaInspectorApplyStatus = ""
        mediaInspectorTechnical = documentController.inspectMediaAssetTechnicalMetadata(asset)
        mediaMetadataInspectorPopup.open()
    }

    function metadataDisplay(value, fallback) {
        const normalized = String(value === undefined || value === null ? "" : value).trim()
        return normalized.length > 0 ? normalized : (fallback || "Not reported")
    }

    function technicalVideoSummary() {
        const metadata = mediaInspectorTechnical || ({})
        const dimensions = Number(metadata.width || 0) > 0 && Number(metadata.height || 0) > 0
                ? String(metadata.width) + " × " + String(metadata.height) : "Resolution not reported"
        const frameRate = Number(metadata.frameRate || 0) > 0
                ? Number(metadata.frameRate).toFixed(3).replace(/\.000$/, "") + " fps"
                : "Frame rate not reported"
        return metadataDisplay(metadata.videoCodec, "Codec not reported").toUpperCase()
                + "  ·  " + dimensions + "  ·  " + frameRate
    }

    function technicalAudioSummary() {
        const metadata = mediaInspectorTechnical || ({})
        if (metadata.hasAudio !== true)
            return "No audio stream"
        const channels = Number(metadata.audioChannels || 0) > 0
                ? String(metadata.audioChannels) + (Number(metadata.audioChannels) === 1 ? " channel" : " channels")
                : "Channels not reported"
        const rate = Number(metadata.audioSampleRate || 0) > 0
                ? String(metadata.audioSampleRate) + " Hz" : "Sample rate not reported"
        return metadataDisplay(metadata.audioCodec, "Codec not reported").toUpperCase()
                + "  ·  " + channels + "  ·  " + rate
    }

    function technicalColorMetadataTagSummary() {
        const metadata = mediaInspectorTechnical || ({})
        const streamTags = metadata.streamColorMetadataTags || ({})
        const formatTags = metadata.formatColorMetadataTags || ({})
        const values = []
        const streamKeys = Object.keys(streamTags)
        const formatKeys = Object.keys(formatTags)
        for (let index = 0; index < streamKeys.length; ++index)
            values.push("stream " + streamKeys[index] + "=" + String(streamTags[streamKeys[index]]))
        for (let index = 0; index < formatKeys.length; ++index)
            values.push("file " + formatKeys[index] + "=" + String(formatTags[formatKeys[index]]))
        return values.join("  ·  ")
    }

    function hasTechnicalColorMetadataTags() {
        const metadata = mediaInspectorTechnical || ({})
        return Object.keys(metadata.streamColorMetadataTags || ({})).length > 0
                || Object.keys(metadata.formatColorMetadataTags || ({})).length > 0
    }

    function sourceColorTransformName(transform) {
        const profile = String(transform || "")
        if (profile === "slog2-sgamut")
            return "Sony S-Log2 / S-Gamut → Rec.709"
        if (profile === "slog3-sgamut3")
            return "Sony S-Log3 / S-Gamut3 → Rec.709"
        if (profile === "slog3-sgamut3-cine")
            return "Sony S-Log3 / S-Gamut3.Cine → Rec.709"
        return ""
    }

    function canApplySourceColorSuggestion() {
        const profile = String(mediaInspectorTechnical.suggestedVideoInputTransform || "")
        const clip = documentController ? (documentController.currentClip || ({})) : ({})
        return profile.length > 0 && clip.selected === true
                && clip.mediaType === "video"
                && String(clip.assetId || "") === String(mediaInspectorAsset.assetId || "")
    }

    function applySourceColorSuggestion() {
        if (!canApplySourceColorSuggestion())
            return
        const profile = String(mediaInspectorTechnical.suggestedVideoInputTransform || "")
        if (documentController.setSelectedClipVideoInputTransform(profile))
            mediaInspectorApplyStatus = "Applied to the selected timeline clip. This edit supports Undo."
    }

    function smartCollectionName(collectionId) {
        const collections = documentController ? documentController.smartCollections : []
        for (let index = 0; index < collections.length; ++index) {
            if (String(collections[index].collectionId) === String(collectionId || ""))
                return String(collections[index].name)
        }
        return "Smart Collection"
    }

    function resetSmartCollectionDraft() {
        smartCollectionEditingId = ""
        smartCollectionNameDraft = ""
        smartCollectionMediaTypeDraft = "any"
        smartCollectionRatingDraft = 0
        smartCollectionLabelDraft = "any"
        smartCollectionTagDraft = ""
        smartCollectionDateFromDraft = ""
        smartCollectionDateToDraft = ""
        smartCollectionMinimumDurationDraft = ""
        smartCollectionMaximumDurationDraft = ""
        smartCollectionOrientationDraft = "any"
        smartCollectionOnlineDraft = "any"
        smartCollectionUsageDraft = "any"
        smartCollectionCustomFieldDraft = ""
        smartCollectionCustomValueDraft = ""
    }

    function editSmartCollection(collection) {
        const rules = collection.rules || ({})
        smartCollectionEditingId = String(collection.collectionId || "")
        smartCollectionNameDraft = String(collection.name || "")
        smartCollectionMediaTypeDraft = String(rules.mediaType || "any")
        smartCollectionRatingDraft = Number(rules.minimumRating || 0)
        smartCollectionLabelDraft = String(rules.colorLabel || "any")
        smartCollectionTagDraft = String(rules.tag || "")
        smartCollectionDateFromDraft = String(rules.dateFrom || "")
        smartCollectionDateToDraft = String(rules.dateTo || "")
        smartCollectionMinimumDurationDraft = rules.minimumDuration === undefined
                ? "" : String(rules.minimumDuration)
        smartCollectionMaximumDurationDraft = rules.maximumDuration === undefined
                ? "" : String(rules.maximumDuration)
        smartCollectionOrientationDraft = String(rules.orientation || "any")
        smartCollectionOnlineDraft = String(rules.onlineState || "any")
        smartCollectionUsageDraft = String(rules.usage || "any")
        smartCollectionCustomFieldDraft = String(rules.customField || "")
        smartCollectionCustomValueDraft = String(rules.customValue || "")
    }

    function smartCollectionDraftRules() {
        return {
            "mediaType": smartCollectionMediaTypeDraft,
            "minimumRating": smartCollectionRatingDraft,
            "colorLabel": smartCollectionLabelDraft,
            "tag": smartCollectionTagDraft,
            "dateFrom": smartCollectionDateFromDraft,
            "dateTo": smartCollectionDateToDraft,
            "minimumDuration": Number(smartCollectionMinimumDurationDraft || 0),
            "maximumDuration": Number(smartCollectionMaximumDurationDraft || 0),
            "orientation": smartCollectionOrientationDraft,
            "onlineState": smartCollectionOnlineDraft,
            "usage": smartCollectionUsageDraft,
            "customField": smartCollectionCustomFieldDraft,
            "customValue": smartCollectionCustomValueDraft
        }
    }

    function saveSmartCollectionDraft() {
        if (!documentController)
            return false
        const rules = smartCollectionDraftRules()
        let saved = false
        if (smartCollectionEditingId.length > 0)
            saved = documentController.updateSmartCollection(
                        smartCollectionEditingId, smartCollectionNameDraft, rules)
        else
            saved = documentController.createSmartCollection(
                        smartCollectionNameDraft, rules).length > 0
        if (saved)
            resetSmartCollectionDraft()
        return saved
    }

    function toggleMediaSelection(assetId) {
        const id = String(assetId || "")
        if (id.length === 0)
            return
        let updated = selectedMediaAssetIds.slice(0)
        const index = updated.indexOf(id)
        if (index >= 0)
            updated.splice(index, 1)
        else
            updated.push(id)
        selectedMediaAssetIds = updated
    }

    function selectMediaAsset(assetId, modifiers, filteredIndex) {
        const id = String(assetId || "")
        const index = Number(filteredIndex)
        if (id.length === 0)
            return

        const modifierFlags = Number(modifiers || 0)
        const toggleModifier = (modifierFlags & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
        const shiftModifier = (modifierFlags & Qt.ShiftModifier) !== 0
        if (shiftModifier && mediaSelectionAnchorIndex >= 0
                && mediaSelectionAnchorIndex < filteredMediaModel.count
                && index >= 0 && index < filteredMediaModel.count) {
            const first = Math.min(mediaSelectionAnchorIndex, index)
            const last = Math.max(mediaSelectionAnchorIndex, index)
            let updated = toggleModifier ? selectedMediaAssetIds.slice(0) : []
            for (let filteredIndex = first; filteredIndex <= last; ++filteredIndex) {
                const rangeId = String(filteredMediaModel.get(filteredIndex).assetId || "")
                if (rangeId.length > 0 && updated.indexOf(rangeId) < 0)
                    updated.push(rangeId)
            }
            selectedMediaAssetIds = updated
            return
        }

        if (toggleModifier) {
            toggleMediaSelection(id)
        } else {
            selectedMediaAssetIds = [id]
        }
        if (index >= 0 && index < filteredMediaModel.count)
            mediaSelectionAnchorIndex = index
    }

    function toggleMediaSearchScope(scope) {
        const normalized = String(scope || "all")
        if (normalized === "all") {
            mediaSearchScopes = ["all"]
            return
        }
        let updated = mediaSearchScopes.indexOf("all") >= 0
                ? [] : mediaSearchScopes.slice(0)
        const index = updated.indexOf(normalized)
        if (index >= 0)
            updated.splice(index, 1)
        else
            updated.push(normalized)
        mediaSearchScopes = updated.length > 0 ? updated : ["all"]
    }

    function metadataTargets(assetId) {
        const id = String(assetId || "")
        return selectedMediaAssetIds.indexOf(id) >= 0
                ? selectedMediaAssetIds.slice(0) : [id]
    }

    function applyMediaMetadataAction(assetId, action, value) {
        if (!documentController)
            return
        const targets = metadataTargets(assetId)
        if (!targets.every(function(target) { return root.isCatalogBackedAsset(target) }))
            return
        if (action === "rating")
            documentController.setMediaAssetRating(targets, Number(value || 0))
        else if (action === "disposition")
            documentController.setMediaAssetDisposition(targets, String(value || "none"))
        else if (action === "color")
            documentController.setMediaAssetColorLabel(targets, String(value || "none"))
    }

    function openMediaTagEditor(assetId) {
        const targets = metadataTargets(assetId)
        if (!targets.every(function(target) { return root.isCatalogBackedAsset(target) }))
            return
        if (selectedMediaAssetIds.indexOf(String(assetId || "")) < 0)
            selectedMediaAssetIds = targets
        const metadata = targets.length === 1 ? mediaMetadataFor(targets[0]) : ({})
        mediaTagsDraft = targets.length === 1 ? (metadata.tags || []).join(", ") : ""
        mediaTagsPopup.open()
        Qt.callLater(function() { mediaTagsField.forceActiveFocus() })
    }

    function openTakeComparison() {
        if (!assetCatalog || selectedMediaAssetIds.length < 2)
            return
        const selected = ({})
        for (let selectedIndex = 0; selectedIndex < selectedMediaAssetIds.length; ++selectedIndex)
            selected[String(selectedMediaAssetIds[selectedIndex])] = true
        const takes = []
        for (let index = 0; index < assetCatalog.assetCount && takes.length < 8; ++index) {
            const asset = assetCatalog.assetAt(index)
            if (selected[String(asset.assetId || "")] === true
                    && (asset.mediaType === "video" || asset.mediaType === "photo"))
                takes.push(asset)
        }
        if (takes.length < 2)
            return
        takeComparisonAssets = takes
        takeComparisonPlaying = false
        takeComparisonPopup.open()
    }

    function setTakeComparisonPlaying(playing) {
        takeComparisonPlaying = playing
        for (let index = 0; index < takeComparisonRepeater.count; ++index) {
            const card = takeComparisonRepeater.itemAt(index)
            if (card)
                card.setPlaying(playing)
        }
    }

    function catalogAssetSnapshot() {
        const assets = []
        if (!assetCatalog)
            return assets
        for (let index = 0; index < assetCatalog.assetCount; ++index)
            assets.push(assetCatalog.assetAt(index))
        return assets
    }

    function refreshMediaUsageAnalysis() {
        if (!documentController)
            return
        mediaUsageAnalysis = documentController.analyzeMediaUsage(catalogAssetSnapshot())
    }

    function openMediaUsageAnalysis() {
        mediaUsageFilter = "all"
        mediaUsageCanonicalAssetId = ""
        refreshMediaUsageAnalysis()
        mediaUsagePopup.open()
    }

    function usageItemVisible(item) {
        if (mediaUsageFilter === "all")
            return true
        if (mediaUsageFilter === "used")
            return item.used === true
        if (mediaUsageFilter === "unused")
            return item.unused === true
        if (mediaUsageFilter === "duplicate")
            return item.duplicate === true
        if (mediaUsageFilter === "offline")
            return item.offline === true
        if (mediaUsageFilter === "derivative")
            return item.derivative === true
        return true
    }

    function consolidateUsageGroup(canonical) {
        const items = mediaUsageAnalysis.items || []
        const duplicates = []
        for (let index = 0; index < items.length; ++index) {
            const candidate = items[index]
            if (String(candidate.duplicateGroupKey || "") ===
                    String(canonical.duplicateGroupKey || "")
                    && String(candidate.assetId || "") !== String(canonical.assetId || ""))
                duplicates.push(candidate)
        }
        if (documentController.consolidateDuplicateMedia(canonical, duplicates) > 0) {
            mediaUsageCanonicalAssetId = ""
            refreshMediaUsageAnalysis()
        }
    }

    function openSourceTape() {
        if (!documentController)
            return
        const assets = catalogAssetSnapshot()
        const filtered = []
        for (let index = 0; index < assets.length; ++index) {
            const asset = assets[index]
            if (!documentController.mediaAssetVisibleInBin(
                        String(asset.assetId || ""), documentController.activeMediaBinId))
                continue
            if (!documentController.mediaAssetMatchesSmartCollection(
                        asset, documentController.activeSmartCollectionId))
                continue
            if (mediaSearchText.trim().length > 0
                    && documentController.searchMediaAsset(
                        String(asset.assetId || ""), mediaSearchText,
                        mediaSearchScopes, asset).matches !== true)
                continue
            filtered.push(asset)
        }
        if (documentController.configureSourceTape(filtered) > 0) {
            sourceTapeAutoAdvance = false
            tabChangeRequested("source")
        }
    }

    function syncSourceMonitor() {
        // Media and Source are independently dockable instances. Only the
        // exposed Source viewer owns a decoder; the media browser never does.
        if (!sourceViewerActive) {
            suspendSourceMonitor()
            return
        }
        if (!hasSourceClipValue) {
            cancelSourcePreroll(true)
            sourcePreviewPlayer.stop()
            sourcePreviewPlayer.audioOutput = null
            sourcePreviewPlayer.source = ""
            sourceDecoderPrimed = false
            sourceFreshDecoderPending = false
            previewSeconds = 0
            return
        }

        if (!root.sourceUsesPlayer()) {
            cancelSourcePreroll(true)
            sourcePreviewPlayer.stop()
            sourcePreviewPlayer.audioOutput = null
            sourcePreviewPlayer.source = ""
            sourceDecoderPrimed = false
            sourceFreshDecoderPending = false
            previewSeconds = Math.max(0, sourceInSecondsValue)
            return
        }

        const changedSource = String(sourcePreviewPlayer.source) !== String(sourceState.sourceUrl || "")
        if (changedSource) {
            cancelSourcePreroll(true)
            sourcePreviewPlayer.pause()
            sourcePreviewPlayer.source = sourceState.sourceUrl || ""
        }
        if (changedSource || suspendedSourceSeconds >= 0)
            root.seekSource(suspendedSourceSeconds >= 0 && suspendedSourceUrl === String(sourceState.sourceUrl || "")
                            ? suspendedSourceSeconds : Math.max(0, sourceInSecondsValue))
        suspendedSourceSeconds = -1
    }

    function suspendSourceMonitor() {
        if (String(sourcePreviewPlayer.source).length > 0) {
            suspendedSourceSeconds = Math.max(0, previewSeconds)
            suspendedSourceUrl = String(sourcePreviewPlayer.source)
        }
        sourceTapeAutoAdvance = false
        cancelSourcePreroll(false)
        sourceUserPlaybackRequested = false
        sourcePreviewPlayer.audioOutput = null
        sourcePreviewPlayer.stop()
        sourcePreviewPlayer.source = ""
        sourceDecoderPrimed = false
        sourceFreshDecoderPending = false
        sourceVideoMonitor.clearOutput()
        sourceFrameAvailable = false
    }

    function suspendForRelease() {
        sourceTapeAutoAdvance = false
        setTakeComparisonPlaying(false)
        pauseSourceMonitor()
    }

    function pauseSourceMonitor() {
        sourceTapeAutoAdvance = false
        if (String(sourcePreviewPlayer.source).length > 0) {
            suspendedSourceSeconds = Math.max(
                        0, sourcePrerollActive ? sourcePrerollTargetSeconds : previewSeconds)
            suspendedSourceUrl = String(sourcePreviewPlayer.source)
            cancelSourcePreroll(false)
            sourceUserPlaybackRequested = false
            sourcePreviewPlayer.pause()
            previewSeconds = suspendedSourceSeconds
        }
        sourceResourceReleaseTimer.restart()
    }

    onSourceViewerActiveChanged: {
        if (sourceViewerActive) {
            sourceResourceReleaseTimer.stop()
            syncSourceMonitor()
        } else {
            pauseSourceMonitor()
        }
    }

    Timer {
        id: sourceResourceReleaseTimer
        interval: 2000
        repeat: false
        onTriggered: if (!root.sourceViewerActive) root.suspendSourceMonitor()
    }

    Timer {
        id: sourcePrerollSeekTimer
        interval: 35
        repeat: false
        onTriggered: root.issueSourcePrerollSeek(root.sourcePrerollSeekTimerGeneration)
    }

    Timer {
        id: sourcePrerollDeadlineTimer
        interval: 2000
        repeat: false
        onTriggered: root.failSourcePreroll(root.sourcePrerollDeadlineGeneration)
    }

    onMediaBrowserActiveChanged: {
        if (mediaBrowserActive) {
            if (projectReferencedMediaSnapshotDirty)
                requestProjectReferencedMediaRefresh()
            if (mediaFilterActiveRevision !== mediaFilterRevision || mediaFilterBusy)
                mediaFilterRefreshTimer.restart()
            mediaVisibleRangeTimer.restart()
        } else {
            projectMediaRefreshTimer.stop()
            mediaFilterRefreshTimer.stop()
            mediaFilterBatchTimer.stop()
            mediaVisibleRangeTimer.stop()
        }
    }

    function scrollEditorToTop() {
        const flickable = sourceEditorScroll.contentItem
        if (!flickable)
            return false
        flickable.contentY = 0
        return true
    }

    function scrollEditorToBottom() {
        const flickable = sourceEditorScroll.contentItem
        if (!flickable)
            return false
        flickable.contentY = Math.max(0, flickable.contentHeight - flickable.height)
        return true
    }

    ListModel {
        id: filteredMediaModel
    }

    Timer {
        id: mediaFilterRefreshTimer
        interval: 90
        repeat: false
        onTriggered: root.refreshFilteredMediaModel()
    }

    Timer {
        id: projectMediaRefreshTimer
        interval: 90
        repeat: false
        onTriggered: root.refreshProjectReferencedMedia()
    }

    Timer {
        id: mediaFilterBatchTimer
        interval: 1
        repeat: false
        onTriggered: root.appendFilteredMediaBatch()
    }

    Timer {
        id: mediaVisibleRangeTimer
        interval: 32
        repeat: false
        onTriggered: root.refreshVisibleMediaRange()
    }

    onMediaSearchTextChanged: {
        mediaGridScrollPosition = 0
        mediaListScrollPosition = 0
        scheduleMediaFilterRefresh()
    }
    onMediaSearchScopesChanged: {
        mediaGridScrollPosition = 0
        mediaListScrollPosition = 0
        scheduleMediaFilterRefresh()
    }
    onMediaViewModeChanged: mediaVisibleRangeTimer.restart()
    onMediaThumbnailTargetSizeChanged: mediaVisibleRangeTimer.restart()
    onActiveTabChanged: {
        mediaVisibleRangeTimer.restart()
        if (activeTab === "media")
            requestProjectReferencedMediaRefresh()
    }
    onCollapsedChanged: {
        mediaVisibleRangeTimer.restart()
        if (mediaBrowserActive)
            requestProjectReferencedMediaRefresh()
    }
    onWorkspaceActiveChanged: {
        if (mediaBrowserActive)
            requestProjectReferencedMediaRefresh()
    }
    onTimelineDocumentChanged: {
        requestProjectReferencedMediaRefresh()
    }

    MediaPlayer {
        id: sourcePreviewPlayer
        objectName: "timelineSourcePreviewPlayer"
        audioOutput: sourcePreviewAudio
        videoOutput: sourceVideoMonitor
        onSourceChanged: {
            root.cancelSourcePreroll(true)
            root.sourceDecoderPrimed = false
            root.sourceFreshDecoderPending = String(source).length > 0
                    && root.hasSource && root.sourceState.mediaType === "video"
            root.sourcePrerollFailed = false
            root.sourceUserPlaybackRequested = false
            sourceVideoMonitor.clearOutput()
            root.sourceFrameAvailable = false
        }

        onPositionChanged: function(position) {
            root.previewSeconds = root.sourcePrerollActive
                    ? root.sourcePrerollTargetSeconds : position / 1000
        }
        onPlaybackStateChanged: function(playbackState) {
            if (root.sourcePrerollPlaybackHandoffPending) {
                root.finishSourcePrerollPlaybackHandoff(
                            root.sourcePrerollPlaybackHandoffGeneration, playbackState)
                return
            }
            if (playbackState !== MediaPlayer.PlayingState && root.hasSourceClipValue) {
                if (root.sourcePrerollActive) {
                    root.previewSeconds = root.sourcePrerollTargetSeconds
                    return
                }
                root.previewSeconds = sourcePreviewPlayer.position / 1000
                root.sourceUserPlaybackRequested = false
            }
        }
        onMediaStatusChanged: function(mediaStatus) {
            root.maybeStartSourcePrerollPlayback()
            if (mediaStatus === MediaPlayer.EndOfMedia && root.sourceTapeEnabledValue
                    && !root.sourcePrerollActive) {
                const continuePlayback = root.sourceTapeAutoAdvance
                if (root.documentController.stepSourceTape(1) && continuePlayback)
                    Qt.callLater(function() { if (root.visible) sourcePreviewPlayer.play() })
                else
                    root.sourceTapeAutoAdvance = false
            }
        }
        onSeekableChanged: root.maybeStartSourcePrerollPlayback()
        onDurationChanged: root.maybeStartSourcePrerollPlayback()
    }

    onVisibleChanged: {
        if (!visible) {
            projectMediaRefreshTimer.stop()
            sourceTapeAutoAdvance = false
            setTakeComparisonPlaying(false)
            pauseSourceMonitor()
            if (workspaceActive && activeTab === "media" && assetCatalog
                    && typeof assetCatalog.setVisibleAssetRange === "function")
                assetCatalog.setVisibleAssetRange(-1, -1)
        } else {
            mediaVisibleRangeTimer.restart()
            if (mediaBrowserActive)
                requestProjectReferencedMediaRefresh()
        }
    }

    Connections {
        target: sourceVideoMonitor.videoSink
        function onVideoFrameChanged() {
            root.handleSourceVideoFrame()
        }
    }

    AudioOutput {
        id: sourcePreviewAudio
        volume: 1.0
        muted: root.programGangEnabled || root.sourcePrerollActive
    }

    onProgramGangEnabledChanged: {
        if (programGangEnabled) {
            sourceTapeAutoAdvance = false
            cancelSourcePreroll(true)
            sourceUserPlaybackRequested = false
            sourcePreviewPlayer.pause()
        } else {
            if (sourceFreshDecoderPending && sourceViewerActive)
                beginSourcePreroll(previewSeconds)
        }
    }

    onProgramPlayingChanged: {
        if (programPlaying && (sourcePrerollActive || sourcePrerollPlaybackHandoffPending)) {
            const target = sourcePrerollActive
                    ? sourcePrerollTargetSeconds : sourcePrerollPlaybackHandoffTargetSeconds
            cancelSourcePreroll(true)
            sourceUserPlaybackRequested = false
            sourcePrerollFailed = false
            sourcePreviewPlayer.pause()
            previewSeconds = target
        } else if (!programPlaying && sourceFreshDecoderPending && sourceViewerActive
                && !programGangEnabled && !sourceUserPlaybackRequested)
            beginSourcePreroll(previewSeconds)
    }

    Connections {
        target: root.documentController
        ignoreUnknownSignals: true

        function onSourceMonitorChanged() {
            root.syncSourceMonitor()
        }

        function onSourceSeekRequested(seconds) {
            root.seekSource(seconds)
        }

        function onMediaMetadataChanged() {
            root.mediaMetadataRevision += 1
            root.scheduleMediaFilterRefresh()
        }

        function onSmartCollectionsChanged() {
            root.mediaMetadataRevision += 1
            root.scheduleMediaFilterRefresh()
        }

        function onActiveMediaBinChanged() {
            root.scheduleMediaFilterRefresh()
        }

        function onClipsChanged() {
            root.requestProjectReferencedMediaRefresh()
        }

        function onSequenceChanged() {
            root.requestProjectReferencedMediaRefresh()
        }

        function onTimelineStateChanged() {
            root.requestProjectReferencedMediaRefresh()
        }
    }

    Connections {
        target: root.assetCatalog
        ignoreUnknownSignals: true

        function onDataChanged() {
            root.mediaCatalogRevision += 1
            root.recountProjectOnlyMedia()
            root.scheduleMediaFilterRefresh()
        }
        function onAssetCountChanged() {
            root.recountProjectOnlyMedia()
            root.scheduleMediaFilterRefresh()
        }
        function onTotalAssetCountChanged() {
            root.recountProjectOnlyMedia()
            root.scheduleMediaFilterRefresh()
        }
        function onMediaDurationsChanged() {
            root.mediaMetadataRevision += 1
            root.scheduleMediaFilterRefresh()
        }
    }

    Component.onCompleted: {
        root.syncSourceMonitor()
        root.requestProjectReferencedMediaRefresh()
        root.scheduleMediaFilterRefresh()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.panelPaddingCompact
        spacing: theme.gapRelated

        RowLayout {
            visible: !root.embeddedDockHeader
            Layout.fillWidth: true
            spacing: theme.gapDense

            Label {
                visible: !collapsed
                text: "Source & Media"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                objectName: "timelineSourceCollapseButton"
                iconName: collapsed ? "chevron-right" : "chevron-left"
                showText: false
                compact: true
                tooltipText: collapsed ? "Expand source panel" : "Collapse source panel"
                onClicked: toggleCollapseRequested()
            }
        }

        ColumnLayout {
            visible: collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: theme.space10

            ActionButton {
                Layout.fillWidth: true
                iconName: "source"
                showText: false
                compact: true
                tooltipText: "Source monitor"
                active: activeTab === "source"
                onClicked: tabChangeRequested("source")
            }

            ActionButton {
                Layout.fillWidth: true
                iconName: "media"
                showText: false
                compact: true
                tooltipText: "Media browser"
                active: activeTab === "media"
                onClicked: tabChangeRequested("media")
            }

            Item { Layout.fillHeight: true }

            ActionButton {
                Layout.fillWidth: true
                iconName: "plus"
                showText: false
                compact: true
                tooltipText: "Open media picker"
                tone: "primary"
                onClicked: mediaPickerRequested()
            }
        }

        RowLayout {
            visible: !collapsed && !root.embeddedDockHeader
            Layout.fillWidth: true
            spacing: theme.gapDense

            FilterChip {
                objectName: "timelineSourceTab"
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                text: "Source"
                active: activeTab === "source"
                onClicked: tabChangeRequested("source")
            }

            FilterChip {
                objectName: "timelineMediaTab"
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                text: "Media"
                active: activeTab === "media"
                onClicked: tabChangeRequested("media")
            }

            ActionButton {
                objectName: "timelineImportMediaButton"
                visible: activeTab === "media" && root.browsableMediaCountValue > 0
                text: "Import"
                iconName: "plus"
                compact: true
                tooltipText: "Import media into the library"
                tone: "neutral"
                onClicked: importWorkspaceRequested()
            }
        }

        Item {
            visible: !collapsed && activeTab === "source"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            Layout.minimumHeight: 0
            implicitWidth: 1
            implicitHeight: 1

            ScrollView {
                id: sourceEditorScroll
                anchors.fill: parent
                objectName: "timelineSourceEditorScroll"
                clip: true
                contentWidth: Math.max(0, availableWidth
                                        - (sourceEditorVerticalBar.visible
                                           ? sourceEditorVerticalBar.width : 0))
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: ScrollBar {
                    id: sourceEditorVerticalBar
                    policy: ScrollBar.AlwaysOn
                    visible: sourceEditorScroll.contentItem
                             && sourceEditorScroll.contentItem.contentHeight
                                > sourceEditorScroll.contentItem.height + 1
                    interactive: true
                    Accessible.name: "Scroll Source monitor controls"
                }

                Rectangle {
                    width: Math.max(0, sourceEditorScroll.availableWidth
                                    - (sourceEditorVerticalBar.visible
                                       ? sourceEditorVerticalBar.width : 0))
                    height: implicitHeight
                    radius: 0
                    color: "transparent"
                    border.width: 0
                    implicitHeight: sourceEditorBody.implicitHeight + theme.panelPaddingCompact * 2

                    ColumnLayout {
                        id: sourceEditorBody
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: theme.panelPaddingCompact
                        spacing: theme.gapDense

                        RowLayout {
                            visible: root.sourceTapeEnabledValue
                            Layout.fillWidth: true
                            spacing: theme.space4

                            Rectangle {
                                objectName: "timelineSourceTapeBar"
                                visible: root.sourceTapeEnabledValue
                                Layout.fillWidth: true
                                implicitHeight: sourceTapeBarRow.implicitHeight + theme.space8
                                radius: theme.radius6
                                color: theme.alpha(theme.ink800, 0.72)
                                border.width: 0

                                RowLayout {
                                    id: sourceTapeBarRow
                                    anchors.fill: parent
                                    anchors.margins: theme.space4
                                    spacing: theme.space4

                                    ActionButton {
                                        objectName: "timelineSourceTapePreviousButton"
                                        iconName: "step-back"
                                        showText: false
                                        compact: true
                                        enabled: root.sourceTapeIndexValue > 0
                                        tooltipText: "Previous Source Tape item"
                                        onClicked: root.documentController.stepSourceTape(-1)
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: "SOURCE TAPE  " + String(root.sourceTapeIndexValue + 1)
                                              + " / " + String(root.sourceTapeItemsValue.length)
                                        color: theme.textStrong
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeCaption
                                        font.weight: Font.DemiBold
                                    }

                                    ActionButton {
                                        objectName: "timelineSourceTapeNextButton"
                                        iconName: "step-forward"
                                        showText: false
                                        compact: true
                                        enabled: root.sourceTapeIndexValue + 1 < root.sourceTapeItemsValue.length
                                        tooltipText: "Next Source Tape item"
                                        onClicked: root.documentController.stepSourceTape(1)
                                    }

                                    ActionButton {
                                        objectName: "timelineSourceTapeExitButton"
                                        text: "Exit"
                                        compact: true
                                        onClicked: {
                                            root.sourceTapeAutoAdvance = false
                                            root.documentController.clearSourceTape()
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space4

                            Label {
                                id: popupTooltipAnchor1
                                objectName: "timelineSourceTitleLabel"
                                text: root.hasSource && sourceState.displayLabel ? sourceState.displayLabel : "Source Monitor"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeBody
                                font.weight: Font.Medium
                                maximumLineCount: 1
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Accessible.name: root.hasSource
                                               ? "Source media: " + String(sourceState.displayLabel || "Media")
                                               : "Source monitor"
                                Accessible.role: Accessible.StaticText
                                AppToolTip {
                                    id: sharedTooltip2
                                    targetItem: popupTooltipAnchor1
                                    visible: popupTooltipAnchor1.visible && (sourceTitleHover.hovered && truncated)
                                    text: popupTooltipAnchor1.text
                                }
                                HoverHandler {
                                    id: sourceTitleHover
                                    enabled: parent.truncated
                                }
                            }

                            StatusChip {
                                objectName: "timelineSourceAnalyzingState"
                                visible: root.sourceAnalysisPending
                                text: sourceState.mediaAnalysisStatus === "queued" ? "QUEUED" : "ANALYZING"
                                tone: "neutral"
                                minimumReadableWidth: 64
                                maximumReadableWidth: 82
                                tooltipText: "Duration, audio presence, dimensions, and poster are resolving in the background."
                            }

                            ActionButton {
                                objectName: "timelineSourceProgramGangButton"
                                iconName: "link"
                                showText: false
                                compact: true
                                active: root.programGangEnabled
                                enabled: root.documentController !== null
                                         && Number(root.documentController.totalDurationSeconds || 0) > 0
                                disabledReason: "Add picture media to Program before ganging the monitors."
                                tooltipText: root.programGangEnabled
                                             ? "Ungang Source and Program monitors"
                                             : "Gang Source to the exact Program frame"
                                onClicked: root.programGangToggleRequested(!root.programGangEnabled)
                            }

                            Label {
                                visible: root.hasSourceClipValue
                                text: timecodeText(previewSeconds)
                                color: theme.accentWarm300
                                font.family: theme.fontNumeric
                                font.features: theme.numericFontFeatures
                                font.pixelSize: root.width < 320 ? theme.typeMicro : theme.typeCaption
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                Layout.rightMargin: 0
                                Layout.minimumWidth: theme.scaled(96)
                                Layout.preferredWidth: theme.scaled(96)
                                horizontalAlignment: Text.AlignRight
                                Accessible.name: "Source timecode " + text
                                Accessible.role: Accessible.StaticText
                            }
                        }

                        Rectangle {
                            objectName: "timelineSourceMonitor"
                            Layout.fillWidth: true
                            Layout.preferredHeight: root.hasSource
                                                    && Number(sourceState.sourceWidth || 0) > 0
                                                    && Number(sourceState.sourceHeight || 0) > 0
                                                    ? Math.max(140, Math.min(
                                                        240,
                                                        width * Number(sourceState.sourceHeight)
                                                        / Number(sourceState.sourceWidth)))
                                                    : theme.scaled(96)
                            radius: theme.radiusSurface
                            color: theme.mediaCanvas
                            clip: true

                            VideoOutput {
                                id: sourceVideoMonitor
                                objectName: "timelineSourceVideoOutput"
                                anchors.fill: parent
                                anchors.margins: theme.panelPaddingCompact
                                visible: root.sourceUsesPlayer() && root.hasSource
                                         && sourceState.mediaType === "video" && root.sourceFrameAvailable
                                opacity: root.sourcePrerollActive ? 0 : 1
                                fillMode: VideoOutput.PreserveAspectFit
                            }

                            Image {
                                objectName: "timelineSourcePosterImage"
                                anchors.fill: parent
                                anchors.margins: theme.panelPaddingCompact
                                source: root.sourcePreviewImage()
                                fillMode: Image.PreserveAspectFit
                                visible: root.hasSource && String(source).length > 0
                                         && (!root.sourceUsesPlayer()
                                             || (sourceState.mediaType === "video"
                                                 && !root.sourceFreshDecoderPending
                                                 && !root.sourcePrerollActive
                                                 && !root.sourceFrameAvailable))
                                cache: false
                                asynchronous: true
                            }

                            Label {
                                objectName: "timelineSourceFrameLoadingLabel"
                                anchors.centerIn: parent
                                visible: root.sourceViewerActive && root.hasSource
                                         && root.sourceUsesPlayer() && sourceState.mediaType === "video"
                                         && (root.sourceFreshDecoderPending || root.sourcePrerollActive)
                                text: root.sourcePrerollFailed ? "Frame unavailable" : "Loading preview…"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                visible: !root.hasSourceClipValue
                                width: Math.min(parent.width - theme.space24, theme.scaled(220))
                                spacing: theme.space8

                                Label {
                                    Layout.fillWidth: true
                                    text: "Choose media to preview and mark"
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeBody
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                }

                                ActionButton {
                                    objectName: "timelineSourceEmptyBrowseButton"
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "Browse Media"
                                    iconName: "media"
                                    compact: true
                                    tooltipText: "Choose an existing library clip or import a new one."
                                    onClicked: root.tabChangeRequested("media")
                                }
                            }

                            Rectangle {
                                objectName: "timelineSourceProgramGangOverlay"
                                visible: root.programGangEnabled
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: theme.space8
                                width: Math.min(parent.width - theme.space16, gangOverlayContent.implicitWidth + theme.space16)
                                height: gangOverlayContent.implicitHeight + theme.space10
                                radius: theme.radius4
                                color: theme.alpha(theme.ink950, 0.86)
                                border.width: 0

                                ColumnLayout {
                                    id: gangOverlayContent
                                    anchors.fill: parent
                                    anchors.margins: theme.space6
                                    spacing: theme.space2

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: theme.space6

                                        StatusChip {
                                            objectName: "timelineSourceProgramGangStatus"
                                            text: root.programGangState.matched === true ? "GANGED" : "NO MATCH"
                                            tone: root.programGangState.matched === true ? "success" : "warning"
                                            compact: true
                                            tooltipText: String(root.programGangState.status || "Program frame is not matched")
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: String(root.programGangState.status || "Waiting for Program")
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeMicro
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Label {
                                        id: popupTooltipAnchor3
                                        objectName: "timelineSourceProgramGangTimeMap"
                                        visible: root.programGangState.matched === true
                                        text: "P " + root.timecodeText(root.programGangState.sequenceSeconds)
                                              + "  ↔  S " + root.timecodeText(root.programGangState.sourceSeconds)
                                        color: theme.textMuted
                                        font.family: theme.fontNumeric
                                        font.features: theme.numericFontFeatures
                                        font.pixelSize: theme.typeMicro
                                        maximumLineCount: 1
                                        elide: Text.ElideRight
                                        AppToolTip {
                                            id: sharedTooltip4
                                            targetItem: popupTooltipAnchor3
                                            visible: popupTooltipAnchor3.visible && (gangTimeMapHover.hovered)
                                            text: "Program " + root.timecodeText(root.programGangState.sequenceSeconds)
                                                      + " maps to Source " + root.timecodeText(root.programGangState.sourceSeconds)
                                                      + " at " + Number(root.programGangState.instantaneousRate || 0).toFixed(2) + "×"
                                        }
                                        HoverHandler { id: gangTimeMapHover }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            objectName: "timelineSourceTransportAndMarks"
                            visible: root.hasSourceClipValue
                            Layout.fillWidth: true
                            spacing: theme.space4

                            RowLayout {
                                objectName: "timelineSourceFrameTransport"
                                Layout.alignment: Qt.AlignHCenter
                                spacing: theme.gapDense

                                ActionButton {
                                    objectName: "timelineSourceStepBackButton"
                                    iconName: "step-back"
                                    showText: false
                                    compact: true
                                    tooltipText: "Previous source frame"
                                    enabled: root.hasSourceClipValue
                                    disabledReason: "Load a clip into Source to step frames."
                                    onClicked: root.seekSource(root.previewSeconds - 1 / Math.max(1, root.framesPerSecondValue), true)
                                }

                                ActionButton {
                                    objectName: "timelineSourcePlayButton"
                                    iconName: root.programGangEnabled
                                              ? (root.programPlaying ? "pause" : "play")
                                              : (root.sourcePrerollActive
                                                 ? (root.sourcePrerollPlayAfterSeek ? "pause" : "play")
                                                 : (root.sourcePrerollPlaybackHandoffPending
                                                    || sourcePreviewPlayer.playbackState === MediaPlayer.PlayingState
                                                    ? "pause" : "play"))
                                    showText: false
                                    compact: true
                                    tooltipText: root.programGangEnabled
                                                 ? (root.programPlaying ? "Pause ganged Program" : "Play ganged Program")
                                                 : (root.sourcePrerollActive
                                                    ? (root.sourcePrerollPlayAfterSeek
                                                       ? "Pause after source seek" : "Play source")
                                                    : (root.sourcePrerollPlaybackHandoffPending
                                                       || sourcePreviewPlayer.playbackState === MediaPlayer.PlayingState
                                                       ? "Pause source" : "Play source"))
                                    enabled: root.hasSourceClipValue
                                    disabledReason: "Load a clip into Source to use playback."
                                    onClicked: {
                                        if (root.programGangEnabled) {
                                            root.gangedPlaybackToggleRequested()
                                            return
                                        }
                                        if (root.sourcePrerollActive) {
                                            root.sourcePrerollPlayAfterSeek =
                                                    !root.sourcePrerollPlayAfterSeek
                                            root.sourceUserPlaybackRequested =
                                                    root.sourcePrerollPlayAfterSeek
                                            root.sourceTapeAutoAdvance = false
                                        } else if (root.sourcePrerollPlaybackHandoffPending) {
                                            root.clearSourcePrerollPlaybackHandoff()
                                            root.sourceUserPlaybackRequested = false
                                            root.sourceTapeAutoAdvance = false
                                        } else if (root.sourceFreshDecoderPending &&
                                                   !root.sourceDecoderPrimed &&
                                                   root.sourceState.mediaType === "video") {
                                            if (!root.requestSourcePlaybackAfterFreshPreroll())
                                                root.sourcePrerollFailed = true
                                        } else if (sourcePreviewPlayer.playbackState === MediaPlayer.PlayingState) {
                                            root.sourceTapeAutoAdvance = false
                                            root.sourceUserPlaybackRequested = false
                                            sourcePreviewPlayer.pause()
                                        } else {
                                            root.sourcePrerollFailed = false
                                            root.sourceUserPlaybackRequested = true
                                            root.restoreSourceAudioOutput()
                                            root.sourceTapeAutoAdvance = root.sourceTapeEnabledValue
                                            sourcePreviewPlayer.play()
                                        }
                                    }
                                }

                                ActionButton {
                                    objectName: "timelineSourceStepForwardButton"
                                    iconName: "step-forward"
                                    showText: false
                                    compact: true
                                    tooltipText: "Step forward one frame"
                                    enabled: root.hasSourceClipValue
                                    disabledReason: "Load a clip into Source to step frames."
                                    onClicked: root.seekSource(root.previewSeconds + 1 / Math.max(1, root.framesPerSecondValue), true)
                                }
                            }

                            RowLayout {
                                objectName: "timelineSourceMarkControls"
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                ActionButton {
                                    objectName: "timelineSourceMarkInButton"
                                    Layout.fillWidth: true
                                    text: "Mark In"
                                    iconName: "mark-in"
                                    compact: true
                                    active: root.sourceInSecondsValue > 0.001
                                    tooltipText: "Set mark in"
                                    enabled: root.hasSourceClipValue
                                    disabledReason: "Load a clip into Source to set an in point."
                                    onClicked: if (root.documentController) root.documentController.setSourceInSeconds(root.previewSeconds)
                                }

                                ActionButton {
                                    objectName: "timelineSourceMarkOutButton"
                                    Layout.fillWidth: true
                                    text: "Mark Out"
                                    iconName: "mark-out"
                                    compact: true
                                    active: root.hasSource && root.sourceOutSecondsValue
                                            < Number(sourceState.durationSeconds || 0) - 0.001
                                    tooltipText: "Set mark out"
                                    enabled: root.hasSourceClipValue
                                    disabledReason: "Load a clip into Source to set an out point."
                                    onClicked: if (root.documentController) root.documentController.setSourceOutSeconds(root.previewSeconds)
                                }
                            }
                        }

                        ToolValueSlider {
                            objectName: "timelineSourceScrubber"
                            visible: root.hasSourceClipValue
                            Layout.fillWidth: true
                            Layout.preferredHeight: theme.controlHeightTall
                            label: "Source position"
                            valueText: timecodeText(root.previewSeconds)
                            tooltipText: "Scrub source · " + timecodeText(root.previewSeconds)
                            from: 0
                            to: root.hasSource && sourceState.durationSeconds ? sourceState.durationSeconds : 1
                            value: root.previewSeconds
                            enabled: root.hasSource
                            onMoved: root.seekSource(value, true)
                        }

                        ColumnLayout {
                            objectName: "timelineSourceMarkedRange"
                            visible: root.hasSourceClipValue
                            Layout.fillWidth: true
                            spacing: theme.space4

                            Label {
                                objectName: "timelineSourceMarkedDuration"
                                Layout.fillWidth: true
                                text: "MARKED RANGE  ·  "
                                      + timecodeText(Math.max(0, root.sourceOutSecondsValue - root.sourceInSecondsValue))
                                color: theme.textMain
                                font.family: theme.fontNumeric
                                font.features: theme.numericFontFeatures
                                font.pixelSize: theme.typeCaption
                                font.weight: Font.DemiBold
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space6

                                StatusChip {
                                    objectName: "timelineSourceInStatus"
                                    Layout.fillWidth: true
                                    text: "IN  " + timecodeText(root.sourceInSecondsValue)
                                    tone: root.sourceInSecondsValue > 0.001 ? "accent" : "neutral"
                                    iconName: "mark-in"
                                    compact: true
                                }

                                ActionButton {
                                    objectName: "timelineClearSourceInButton"
                                    text: "Clear In"
                                    compact: true
                                    tooltipText: "Clear the Source In point and restore the clip start."
                                    enabled: root.sourceInSecondsValue > 0.001
                                    disabledReason: "The In point is already at the source start."
                                    onClicked: if (root.documentController)
                                                   root.documentController.clearSourceInPoint()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space6

                                StatusChip {
                                    objectName: "timelineSourceOutStatus"
                                    Layout.fillWidth: true
                                    text: "OUT  " + timecodeText(root.sourceOutSecondsValue)
                                    tone: root.hasSource && root.sourceOutSecondsValue
                                          < Number(sourceState.durationSeconds || 0) - 0.001
                                          ? "accent" : "neutral"
                                    iconName: "mark-out"
                                    compact: true
                                }

                                ActionButton {
                                    objectName: "timelineClearSourceOutButton"
                                    text: "Clear Out"
                                    compact: true
                                    tooltipText: "Clear the Source Out point and restore the clip end."
                                    enabled: root.hasSource && root.sourceOutSecondsValue
                                             < Number(sourceState.durationSeconds || 0) - 0.001
                                    disabledReason: "The Out point is already at the source end."
                                    onClicked: if (root.documentController)
                                                   root.documentController.clearSourceOutPoint()
                                }
                            }
                        }

                        Flow {
                            visible: root.hasSourceClipValue
                            Layout.fillWidth: true
                            spacing: theme.gapDense

                            ActionButton {
                                objectName: "timelineSourceInsertButton"
                                text: "Insert at Playhead"
                                iconName: "plus"
                                tone: "primary"
                                compact: true
                                enabled: root.sourceEditReady
                                disabledReason: root.sourceEditDisabledReason("insert it")
                                tooltipText: "Insert the marked Source range at the playhead; later clips shift on linked tracks."
                                onClicked: if (root.documentController) root.documentController.insertSourceClipAtPlayhead()
                            }

                            MenuButton {
                                objectName: "timelineSourceEditMenu"
                                text: "Append / Replace"
                                compact: true
                                primaryOpensMenu: true
                                tooltipText: "Append the marked range to the end, or overwrite at the playhead."
                                enabled: root.sourceEditReady
                                menuItems: [
                                    {"key": "append", "label": "Append to End", "iconName": "step-forward"},
                                    {"key": "overwrite", "label": "Overwrite at Playhead", "iconName": "edit"}
                                ]
                                menuWidth: 210
                                onMenuItemTriggered: function(key) {
                                    if (!root.documentController)
                                        return
                                    if (key === "append")
                                        root.documentController.appendSourceClipToTimeline()
                                    else if (key === "overwrite")
                                        root.documentController.overwriteSourceClipAtPlayhead()
                                }
                            }
                        }
                    }
                }
            }

        }

        ColumnLayout {
            visible: !collapsed && activeTab === "media"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: theme.gapRelated

            RowLayout {
                visible: root.browsableMediaCountValue > 0
                Layout.fillWidth: true
                spacing: root.width < theme.scaled(400) ? theme.space2 : theme.space4

                ToneTextField {
                    objectName: "timelineUnifiedMediaSearchField"
                    Layout.fillWidth: true
                    accessibleLabel: "Search media"
                    Accessible.description: "Search filenames, metadata, tags, transcript, captions, markers, and on-screen text. Use Search Fields to narrow the search."
                    text: root.mediaSearchText
                    placeholderText: "Search media…"
                    selectByMouse: true
                    onTextChanged: root.mediaSearchText = text
                }

                ActionButton {
                    objectName: "timelineClearMediaSearchButton"
                    visible: root.mediaSearchText.length > 0
                    iconName: "close"
                    showText: false
                    compact: true
                    tooltipText: "Clear media search"
                    onClicked: root.mediaSearchText = ""
                }

                MenuButton {
                    objectName: "timelineMediaSearchScopeMenuButton"
                    text: "Fields"
                    iconName: "filter"
                    compact: true
                    primaryOpensMenu: true
                    active: root.mediaSearchScopes.indexOf("all") < 0
                    accessibleName: root.mediaSearchScopes.indexOf("all") >= 0
                                    ? "Search fields: all fields"
                                    : "Search fields: " + root.mediaSearchScopes.length + " selected"
                    tooltipText: "Choose which media fields are searched. Multiple fields can be combined."
                    menuWidth: 220
                    menuItems: root.mediaSearchScopeMenuItems()
                    onMenuItemTriggered: function(key) { root.toggleMediaSearchScope(key) }
                }
            }

            RowLayout {
                visible: root.browsableMediaCountValue > 0
                Layout.fillWidth: true
                spacing: root.width < theme.scaled(400) ? theme.space2 : theme.space4

                MenuButton {
                    objectName: "timelineActiveMediaBinFilter"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.mediaPoolScopeLabel()
                    iconName: root.documentController
                              && String(root.documentController.activeSmartCollectionId || "").length > 0
                              ? "filter" : "folder"
                    compact: true
                    primaryOpensMenu: true
                    active: root.documentController
                             && (String(root.documentController.activeMediaBinId || "").length > 0
                                 || String(root.documentController.activeSmartCollectionId || "").length > 0)
                    accessibleName: "Filter media pool: " + root.mediaPoolScopeLabel()
                    tooltipText: "Current filter: " + root.mediaPoolScopeLabel()
                                 + ". Choose one bin and one smart collection; these filters combine. Choose an active item again to clear it."
                    menuWidth: 260
                    menuItems: root.mediaPoolFilterMenuItems()
                    onMenuItemTriggered: function(key) { root.applyMediaPoolFilter(key) }
                }

                Label {
                    objectName: "timelineMediaResultCountLabel"
                    text: String(root.mediaResultCountValue) + " / " + String(root.browsableMediaCountValue)
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                    Accessible.name: String(root.mediaResultCountValue) + " matching media items of "
                                     + String(root.browsableMediaCountValue) + " available media items"
                }

                MenuButton {
                    objectName: "timelineMediaPoolBrowseMenuButton"
                    text: "Browse"
                    iconName: "media"
                    showText: root.width >= theme.scaled(400)
                    Layout.preferredWidth: root.width < theme.scaled(400)
                                           ? theme.iconButtonSizeCompact : implicitWidth
                    Layout.minimumWidth: root.width < theme.scaled(400)
                                         ? theme.iconButtonSizeCompact : implicitWidth
                    compact: true
                    primaryOpensMenu: true
                    showMenuIndicator: root.width >= theme.scaled(400)
                    accessibleName: "Browse media tools"
                    menuWidth: 260
                    tooltipText: "Organize bins and collections, start a source tape, review usage, or adjust preview detail."
                    menuItems: root.mediaPoolBrowseMenuItems()
                    onMenuItemTriggered: function(key) { root.handleMediaPoolBrowseAction(key) }
                }
            }

            RowLayout {
                objectName: "timelineMediaViewControls"
                visible: root.browsableMediaCountValue > 0
                Layout.fillWidth: true
                spacing: theme.space6

                Rectangle {
                    implicitWidth: mediaViewModeButtons.implicitWidth + theme.space4 * 2
                    implicitHeight: mediaViewModeButtons.implicitHeight + theme.space4 * 2
                    radius: theme.radius8
                    color: theme.alpha(theme.ink950, 0.3)
                    border.width: theme.hairlineWidth
                    border.color: theme.alpha(theme.lineSoft, 0.28)

                    RowLayout {
                        id: mediaViewModeButtons
                        anchors.fill: parent
                        anchors.margins: theme.space4
                        spacing: theme.space2

                        ActionButton {
                            objectName: "timelineMediaGridViewButton"
                            text: "Grid"
                            iconName: "grid"
                            showText: false
                            compact: true
                            embedded: true
                            active: root.mediaViewMode === "grid"
                            tooltipText: "Show thumbnail grid"
                            accessibleName: "Grid view"
                            onClicked: root.mediaViewMode = "grid"
                        }

                        ActionButton {
                            objectName: "timelineMediaListViewButton"
                            text: "List"
                            iconName: "list"
                            showText: false
                            compact: true
                            embedded: true
                            active: root.mediaViewMode === "list"
                            tooltipText: "Show media list"
                            accessibleName: "List view"
                            onClicked: root.mediaViewMode = "list"
                        }
                    }
                }

                Label {
                    visible: root.mediaViewMode === "grid"
                    text: root.width < theme.scaled(400) ? "Size" : "Thumbnail size"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }

                ToolValueSlider {
                    id: mediaThumbnailSizeSlider
                    objectName: "timelineMediaThumbnailSizeSlider"
                    visible: root.mediaViewMode === "grid"
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(40)
                    Layout.preferredHeight: theme.controlHeightDense
                    minimumReadableWidth: theme.scaled(40)
                    label: root.width < theme.scaled(400) ? "Size" : "Thumbnail size"
                    valueText: Math.round(root.mediaThumbnailTargetSize) + " px"
                    tooltipText: "Resize media cards in the thumbnail grid."
                    from: 112
                    to: 256
                    stepSize: 16
                    live: true
                    value: root.mediaThumbnailTargetSize
                    onMoved: root.mediaThumbnailTargetSize = Math.round(value / stepSize) * stepSize
                }

                Label {
                    visible: root.mediaViewMode === "grid"
                    text: Math.round(root.mediaThumbnailTargetSize) + " px"
                    color: theme.textMuted
                    font.family: theme.fontNumeric
                    font.features: theme.numericFontFeatures
                    font.pixelSize: theme.typeCaption
                }
            }

            ActionButton {
                objectName: "timelineBatchRelinkButton"
                visible: root.documentController
                         && root.documentController.hasOfflineMediaItems
                text: "Relink"
                iconName: "refresh"
                compact: true
                tone: "warning"
                tooltipText: "Search a folder for all offline timeline media and review every substitution before applying."
                onClicked: root.batchRelinkRequested()
            }

            Item {
                objectName: "timelineMediaEmptyState"
                visible: root.browsableMediaCountValue === 0 &&
                         (!root.assetCatalog || !root.assetCatalog.loading)
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - theme.space16, theme.scaled(220))
                    spacing: theme.space8

                    Label {
                        Layout.fillWidth: true
                        text: "No library media"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeControl
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Import files to browse and reuse in this timeline."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ActionButton {
                        objectName: "timelineEmptyMediaImportButton"
                        Layout.alignment: Qt.AlignHCenter
                        text: "Open Import"
                        iconName: "import"
                        compact: true
                        tone: "primary"
                        onClicked: {
                            importWorkspaceRequested()
                        }
                    }
                }
            }

            Label {
                objectName: "timelineMediaLoadingState"
                visible: root.assetCountValue === 0 && root.projectReferencedMedia.length === 0
                         && root.assetCatalog &&
                         root.assetCatalog.loading
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: "Loading library media…"
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeLabel
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Item {
                objectName: "timelineMediaFilterProgressState"
                visible: root.mediaFilterBusy && root.browsableMediaCountValue > 0
                Layout.fillWidth: true
                Layout.fillHeight: true

                Label {
                    anchors.centerIn: parent
                    text: "Filtering media…"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                }
            }

            Item {
                objectName: "timelineMediaNoResultsState"
                visible: !root.mediaFilterBusy && root.browsableMediaCountValue > 0
                         && root.mediaResultCountValue === 0
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - theme.space16, theme.scaled(250))
                    spacing: theme.space8

                    Label {
                        Layout.fillWidth: true
                        text: "No matching media"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeControl
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.mediaSearchText.trim().length > 0
                              ? "Try a different search or clear the active media filters."
                              : "No available media matches the selected bin and collection."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: theme.space6

                        ActionButton {
                            objectName: "timelineResetMediaFiltersButton"
                            text: "Reset Filters"
                            iconName: "close"
                            compact: true
                            tone: "secondary"
                            enabled: root.mediaSearchText.length > 0
                                     || root.mediaSearchScopes.indexOf("all") < 0
                                     || (root.documentController
                                         && (String(root.documentController.activeMediaBinId || "").length > 0
                                             || String(root.documentController.activeSmartCollectionId || "").length > 0))
                            disabledReason: "There are no active search or media filters to reset."
                            onClicked: root.resetMediaFilters()
                        }

                        ActionButton {
                            objectName: "timelineNoResultsBrowseLibraryButton"
                            text: "Browse Library"
                            iconName: "media"
                            compact: true
                            onClicked: mediaPickerRequested()
                        }
                    }
                }
            }

            Rectangle {
                objectName: "timelineMediaMetadataBatchBar"
                visible: root.selectedMediaAssetIds.length > 0
                Layout.fillWidth: true
                implicitHeight: mediaMetadataBatchRow.implicitHeight + theme.space8 * 2
                radius: theme.radiusSurface
                color: theme.alpha(theme.ink800, 0.72)
                border.width: 0

                ColumnLayout {
                    id: mediaMetadataBatchRow
                    anchors.fill: parent
                    anchors.margins: theme.space8
                    spacing: theme.space6

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            id: selectedMediaContextLabel
                            objectName: "timelineSelectedMediaContextLabel"
                            Layout.fillWidth: true
                            text: root.mediaSelectionSummary()
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeLabel
                            font.weight: Font.DemiBold
                            maximumLineCount: 1
                            elide: Text.ElideRight

                            AppToolTip {
                                id: sharedTooltip5
                                targetItem: selectedMediaContextLabel
                                visible: selectedMediaContextLabel.visible && (selectedMediaContextHover.hovered && truncated)
                                text: root.mediaSelectionSummary()
                            }
                            HoverHandler {
                                id: selectedMediaContextHover
                                enabled: selectedMediaContextLabel.truncated
                            }
                        }

                        ActionButton {
                            iconName: "close"
                            showText: false
                            compact: true
                            tooltipText: "Clear media selection"
                            onClicked: {
                                root.selectedMediaAssetIds = []
                                root.mediaSelectionAnchorIndex = -1
                            }
                        }
                    }

                    Flow {
                        objectName: "timelineSelectedMediaActions"
                        Layout.fillWidth: true
                        spacing: theme.gapDense

                        ActionButton {
                            objectName: "timelineMediaSelectedAddToTimelineButton"
                            text: "Add to Timeline"
                            iconName: "plus"
                            tone: "primary"
                            compact: true
                            visible: root.selectedMediaAssetIds.length === 1
                            enabled: root.documentController !== null
                                     && root.selectedMediaOnlineValue
                            disabledReason: root.selectedMediaAssetIds.length !== 1
                                            ? "Select one media item to add it to the timeline."
                                            : "Relink this media before adding it to the timeline."
                            tooltipText: "Insert this media at the playhead and shift later clips on linked tracks."
                            onClicked: root.insertSelectedMediaAtPlayhead()
                        }

                        ActionButton {
                            objectName: "timelineMediaSelectedPreviewInSourceButton"
                            text: "Preview in Source"
                            iconName: "source"
                            tone: "secondary"
                            compact: true
                            visible: root.selectedMediaAssetIds.length === 1
                            enabled: root.documentController !== null
                                     && root.selectedMediaOnlineValue
                            disabledReason: root.selectedMediaAssetIds.length !== 1
                                            ? "Select one media item to preview it in Source."
                                            : "Relink this media before loading it into Source."
                            tooltipText: "Load this media into the Source monitor to inspect and mark a range."
                            onClicked: root.previewSelectedMediaInSource()
                        }

                        Label {
                            objectName: "timelineMediaMultiSelectionActionHint"
                            width: parent.width
                            visible: root.selectedMediaAssetIds.length > 1
                            text: root.selectionIsCatalogBacked()
                                  ? "Batch actions below apply metadata; add clips one at a time."
                                  : "Select one project media item at a time to preview or add it."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.Wrap
                        }
                    }

                    Flow {
                        objectName: "timelineMediaBatchMetadataActions"
                        visible: root.selectionIsCatalogBacked()
                        Layout.fillWidth: true
                        spacing: theme.space4

                        MenuButton {
                        objectName: "timelineMediaBatchRatingButton"
                        text: "Rating"
                        compact: true
                        primaryOpensMenu: true
                        menuWidth: 150
                        menuItems: [
                            {"key": "0", "label": "Unrated", "shortcut": "0"},
                            {"key": "1", "label": "★", "shortcut": "1"},
                            {"key": "2", "label": "★★", "shortcut": "2"},
                            {"key": "3", "label": "★★★", "shortcut": "3"},
                            {"key": "4", "label": "★★★★", "shortcut": "4"},
                            {"key": "5", "label": "★★★★★", "shortcut": "5"}
                        ]
                        onMenuItemTriggered: function(key) {
                            root.documentController.setMediaAssetRating(
                                        root.selectedMediaAssetIds, Number(key))
                        }
                    }

                    MenuButton {
                        objectName: "timelineMediaBatchMarkButton"
                        text: "Mark"
                        compact: true
                        primaryOpensMenu: true
                        menuWidth: 170
                        menuItems: [
                            {"key": "favorite", "label": "Favorite", "shortcut": "F"},
                            {"key": "rejected", "label": "Rejected", "shortcut": "X"},
                            {"key": "none", "label": "Clear Mark"}
                        ]
                        onMenuItemTriggered: function(key) {
                            root.documentController.setMediaAssetDisposition(
                                        root.selectedMediaAssetIds, key)
                        }
                    }

                    MenuButton {
                        objectName: "timelineMediaBatchColorButton"
                        text: "Label"
                        compact: true
                        primaryOpensMenu: true
                        menuWidth: 160
                        menuItems: [
                            {"key": "none", "label": "No Label"},
                            {"key": "red", "label": "Red"},
                            {"key": "orange", "label": "Orange"},
                            {"key": "yellow", "label": "Yellow"},
                            {"key": "green", "label": "Green"},
                            {"key": "blue", "label": "Blue"},
                            {"key": "purple", "label": "Purple"}
                        ]
                        onMenuItemTriggered: function(key) {
                            root.documentController.setMediaAssetColorLabel(
                                        root.selectedMediaAssetIds, key)
                        }
                    }

                    ActionButton {
                        objectName: "timelineMediaBatchTagsButton"
                        text: "Tags"
                        compact: true
                        onClicked: root.openMediaTagEditor(root.selectedMediaAssetIds[0])
                    }

                    ActionButton {
                        objectName: "timelineMediaMetadataInfoButton"
                        visible: root.selectedMediaAssetIds.length === 1
                        text: "Info"
                        iconName: "info"
                        compact: true
                        tooltipText: "Technical metadata, notes, and custom fields"
                        onClicked: root.openMediaMetadataInspector()
                    }

                    ActionButton {
                        objectName: "timelineCompareTakesButton"
                        visible: root.selectedMediaAssetIds.length >= 2
                        text: "Compare"
                        iconName: "compare"
                        compact: true
                        onClicked: root.openTakeComparison()
                    }

                    }
                }
            }

            Loader {
                id: mediaViewLoader
                objectName: "timelineVirtualizedMediaViewLoader"
                visible: !root.mediaFilterBusy && root.mediaResultCountValue > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: visible
                sourceComponent: root.mediaViewMode === "grid"
                                 ? mediaGridViewComponent : mediaListViewComponent
                onLoaded: {
                    const restoredY = root.mediaViewMode === "grid"
                            ? root.mediaGridScrollPosition : root.mediaListScrollPosition
                    item.contentY = restoredY
                    mediaVisibleRangeTimer.restart()
                }
            }

            Component {
                id: mediaGridViewComponent

                GridView {
                    id: timelineVirtualizedMediaGridView
                    objectName: "timelineVirtualizedMediaGrid"
                    readonly property int thumbnailColumnCount: Math.max(1, Math.floor(width / cellWidth))
                    clip: true
                    reuseItems: true
                    cacheBuffer: Math.max(height, theme.scaled(300))
                    cellWidth: Math.min(width, theme.scaled(root.mediaThumbnailTargetSize) + theme.space8)
                    cellHeight: Math.round(cellWidth * 0.74) + theme.scaled(48)
                    boundsBehavior: Flickable.StopAtBounds
                    model: filteredMediaModel
                    delegate: TimelineMediaAssetRow {
                        assetCatalog: root.assetCatalog
                        projectAsset: root.projectAssetForId(assetId)
                        catalogRevision: root.mediaCatalogRevision
                        mediaPanel: root
                        gridPresentation: true
                        thumbnailTargetSize: root.mediaThumbnailTargetSize
                        mediaSearchText: root.mediaSearchText
                        mediaSearchScopes: root.mediaSearchScopes
                        documentController: root.documentController
                        searchRevision: root.mediaMetadataRevision
                        scrubSampleCount: root.mediaFilmstripSampleCount
                        selected: root.selectedMediaAssetIds.indexOf(assetId) >= 0
                        mediaMetadata: root.mediaMetadataFor(assetId)
                        binVisible: true
                        smartCollectionVisible: true
                    }

                    onContentYChanged: {
                        root.mediaGridScrollPosition = contentY
                        mediaVisibleRangeTimer.restart()
                    }
                    onWidthChanged: mediaVisibleRangeTimer.restart()
                    onHeightChanged: mediaVisibleRangeTimer.restart()
                    onMovementEnded: root.refreshVisibleMediaRange()

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        interactive: true
                    }
                }
            }

            Component {
                id: mediaListViewComponent

                ListView {
                    id: timelineVirtualizedMediaListView
                    objectName: "timelineVirtualizedMediaList"
                    clip: true
                    reuseItems: true
                    cacheBuffer: Math.max(height, theme.scaled(320))
                    spacing: theme.gapRelated
                    model: filteredMediaModel
                    delegate: TimelineMediaAssetRow {
                        assetCatalog: root.assetCatalog
                        projectAsset: root.projectAssetForId(assetId)
                        catalogRevision: root.mediaCatalogRevision
                        mediaPanel: root
                        gridPresentation: false
                        mediaSearchText: root.mediaSearchText
                        mediaSearchScopes: root.mediaSearchScopes
                        documentController: root.documentController
                        searchRevision: root.mediaMetadataRevision
                        scrubSampleCount: root.mediaFilmstripSampleCount
                        selected: root.selectedMediaAssetIds.indexOf(assetId) >= 0
                        mediaMetadata: root.mediaMetadataFor(assetId)
                        binVisible: true
                        smartCollectionVisible: true
                    }

                    onContentYChanged: {
                        root.mediaListScrollPosition = contentY
                        mediaVisibleRangeTimer.restart()
                    }
                    onWidthChanged: mediaVisibleRangeTimer.restart()
                    onHeightChanged: mediaVisibleRangeTimer.restart()
                    onMovementEnded: root.refreshVisibleMediaRange()

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        interactive: true
                    }
                }
            }

        }
    }

    AppPopup {
        id: mediaTagsPopup
        objectName: "timelineMediaTagsPopup"
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(380), root.width - theme.space16)))
        x: Math.max(theme.space8, (root.width - width) / 2)
        y: Math.max(theme.space24, (root.height - height) / 2)
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space10

            Label {
                text: "Media Tags"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeTitle
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: "Apply comma-separated tags to " +
                      String(root.selectedMediaAssetIds.length) + " selected item" +
                      (root.selectedMediaAssetIds.length === 1 ? "." : "s.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.Wrap
            }

            ToneTextField {
                id: mediaTagsField
                objectName: "timelineMediaTagsField"
                Layout.fillWidth: true
                text: root.mediaTagsDraft
                placeholderText: "interview, b-roll, approved"
                selectByMouse: true
                onTextChanged: root.mediaTagsDraft = text
                onAccepted: mediaApplyTagsButton.clicked()
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8

                Label {
                    Layout.fillWidth: true
                    text: "T opens tags · 0–5 rates · F favorites · X rejects"
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }

                ActionButton {
                    text: "Cancel"
                    compact: true
                    onClicked: mediaTagsPopup.close()
                }

                ActionButton {
                    id: mediaApplyTagsButton
                    objectName: "timelineMediaApplyTagsButton"
                    text: "Apply Tags"
                    iconName: "check"
                    tone: "primary"
                    compact: true
                    onClicked: {
                        if (root.documentController.setMediaAssetTags(
                                    root.selectedMediaAssetIds,
                                    root.mediaTagsDraft.split(",")))
                            mediaTagsPopup.close()
                    }
                }
            }
        }
    }

    AppPopup {
        id: mediaMetadataInspectorPopup
        objectName: "timelineMediaMetadataInspectorPopup"
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(470), Math.max(theme.scaled(340), root.width - theme.space16))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(650), Math.max(theme.scaled(440), root.height - theme.space24))))
        x: Math.max(theme.space8, (root.width - width) / 2)
        y: Math.max(theme.space12, (root.height - height) / 2)
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space10

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        Layout.fillWidth: true
                        text: root.metadataDisplay(root.mediaInspectorAsset.displayLabel,
                                                   root.mediaInspectorAsset.fileName || "Media Info")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeTitle
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Technical metadata and editorial notes"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                ActionButton {
                    iconName: "close"
                    showText: false
                    compact: true
                    tooltipText: "Close media info"
                    onClicked: mediaMetadataInspectorPopup.close()
                }
            }

            ScrollView {
                id: metadataInspectorScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: metadataInspectorScroll.availableWidth
                    spacing: theme.space10

                    Rectangle {
                        objectName: "timelineMediaTechnicalMetadataCard"
                        Layout.fillWidth: true
                        implicitHeight: technicalMetadataColumn.implicitHeight + theme.space12 * 2
                        radius: theme.radiusSurface
                        color: theme.alpha(theme.ink800, 0.7)
                        border.width: 0

                        ColumnLayout {
                            id: technicalMetadataColumn
                            anchors.fill: parent
                            anchors.margins: theme.space12
                            spacing: theme.space6

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    text: "TECHNICAL"
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeCaption
                                    font.weight: Font.DemiBold
                                }

                                StatusChip {
                                    text: root.mediaInspectorTechnical.available === true ? "Read from file" : "Unavailable"
                                    tone: root.mediaInspectorTechnical.available === true ? "success" : "warning"
                                    compact: true
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.technicalVideoSummary()
                                color: theme.textStrong
                                font.family: theme.fontNumeric
                                font.features: theme.numericFontFeatures
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Color  ·  "
                                      + root.metadataDisplay(root.mediaInspectorTechnical.colorSpace)
                                      + "  ·  "
                                      + root.metadataDisplay(root.mediaInspectorTechnical.colorTransfer)
                                      + "  ·  "
                                      + root.metadataDisplay(root.mediaInspectorTechnical.colorPrimaries)
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: root.hasTechnicalColorMetadataTags()
                                text: "Source color tags  ·  " + root.technicalColorMetadataTagSummary()
                                      + "\nRaw file tags are evidence only; they do not select a camera profile."
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: String(root.mediaInspectorTechnical.suggestedVideoInputTransform || "").length > 0
                                text: "Metadata suggestion  ·  "
                                      + root.sourceColorTransformName(root.mediaInspectorTechnical.suggestedVideoInputTransform)
                                      + "\nReview the source tags before applying this input transform."
                                color: theme.accentCool300
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.technicalAudioSummary()
                                color: theme.textMain
                                font.family: theme.fontNumeric
                                font.features: theme.numericFontFeatures
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Camera  ·  " + root.metadataDisplay(root.mediaInspectorTechnical.camera)
                                      + "  ·  " + root.metadataDisplay(root.mediaInspectorTechnical.lens)
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Captured  ·  " + root.metadataDisplay(root.mediaInspectorTechnical.captureDate)
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.metadataDisplay(root.mediaInspectorTechnical.filePath,
                                                           root.mediaInspectorAsset.filePath || "Path unavailable")
                                color: theme.textFaint
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.WrapAnywhere
                            }

                            Label {
                                visible: root.mediaInspectorTechnical.available !== true
                                Layout.fillWidth: true
                                text: root.metadataDisplay(root.mediaInspectorTechnical.error,
                                                           "The file could not be inspected.")
                                color: theme.warning
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    Label {
                        text: "NOTES"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: Font.DemiBold
                    }

                    ToneTextArea {
                        objectName: "timelineMediaNotesField"
                        accessibleLabel: "Media notes"
                        Layout.fillWidth: true
                        Layout.preferredHeight: theme.scaled(94)
                        text: root.mediaInspectorNotesDraft
                        placeholderText: "Editorial notes, approvals, continuity, or take context"
                        onTextChanged: root.mediaInspectorNotesDraft = text
                    }

                    Label {
                        text: "CUSTOM FIELDS"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: Font.DemiBold
                    }

                    ToneTextArea {
                        objectName: "timelineMediaCustomFieldsField"
                        accessibleLabel: "Media custom fields"
                        Layout.fillWidth: true
                        Layout.preferredHeight: theme.scaled(104)
                        text: root.mediaInspectorCustomFieldsDraft
                        placeholderText: "Client: Northstar\nCampaign: Summer Launch"
                        onTextChanged: root.mediaInspectorCustomFieldsDraft = text
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "One field per line as Name: Value · searchable from Media"
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8

                Label {
                    Layout.fillWidth: true
                    text: root.mediaInspectorApplyStatus.length > 0
                          ? root.mediaInspectorApplyStatus
                          : "Changes support Undo and save with the timeline."
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }

                ActionButton {
                    objectName: "timelineMediaMetadataApplyColorSuggestionButton"
                    visible: root.canApplySourceColorSuggestion()
                    text: "Apply Profile"
                    iconName: "check"
                    tone: "primary"
                    compact: true
                    onClicked: root.applySourceColorSuggestion()
                }

                ActionButton {
                    text: "Cancel"
                    compact: true
                    onClicked: mediaMetadataInspectorPopup.close()
                }

                ActionButton {
                    objectName: "timelineMediaMetadataSaveButton"
                    text: "Save Info"
                    iconName: "check"
                    tone: "primary"
                    compact: true
                    onClicked: {
                        if (root.documentController.setMediaAssetNotesAndCustomFields(
                                    String(root.mediaInspectorAsset.assetId || ""),
                                    root.mediaInspectorNotesDraft,
                                    root.parseCustomFields(root.mediaInspectorCustomFieldsDraft)))
                            mediaMetadataInspectorPopup.close()
                    }
                }
            }
        }
    }

    AppPopup {
        id: takeComparisonPopup
        objectName: "timelineTakeComparisonPopup"
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(720), Math.max(theme.scaled(390), root.width - theme.space16))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(620), Math.max(theme.scaled(420), root.height - theme.space24))))
        x: Math.max(theme.space8, (root.width - width) / 2)
        y: Math.max(theme.space12, (root.height - height) / 2)
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.setTakeComparisonPlaying(false)

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space10

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        text: "Compare Takes"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: String(root.takeComparisonAssets.length) +
                              " takes · playback follows the first take"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                ActionButton {
                    objectName: "timelineTakeComparisonPlayButton"
                    text: root.takeComparisonPlaying ? "Pause" : "Play All"
                    iconName: root.takeComparisonPlaying ? "pause" : "play"
                    tone: "primary"
                    compact: true
                    onClicked: root.setTakeComparisonPlaying(!root.takeComparisonPlaying)
                }

                ActionButton {
                    iconName: "close"
                    showText: false
                    compact: true
                    tooltipText: "Close take comparison"
                    onClicked: takeComparisonPopup.close()
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: takeComparisonGrid.implicitHeight
                clip: true

                GridLayout {
                    id: takeComparisonGrid
                    width: parent.width
                    columns: width >= theme.scaled(620) ? 3 : 2
                    columnSpacing: theme.space8
                    rowSpacing: theme.space8

                    Repeater {
                        id: takeComparisonRepeater
                        model: root.takeComparisonAssets

                        delegate: Rectangle {
                            id: takeCard
                            required property var modelData
                            required property int index
                            readonly property real normalizedPosition: takePlayer.duration > 0
                                                                       ? takePlayer.position / takePlayer.duration : 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: theme.scaled(210)
                            radius: theme.radius8
                            color: theme.alpha(theme.ink850, 0.84)
                            border.width: 0
                            clip: true

                            function setPlaying(playing) {
                                if (modelData.mediaType !== "video")
                                    return
                                if (playing)
                                    takePlayer.play()
                                else
                                    takePlayer.pause()
                            }

                            function seekNormalized(position) {
                                if (takePlayer.duration > 0)
                                    takePlayer.position = Math.round(
                                                Math.max(0, Math.min(1, position))
                                                * takePlayer.duration)
                            }

                            MediaPlayer {
                                id: takePlayer
                                source: String(takeCard.modelData.filePath || "")
                                videoOutput: takeVideo
                                audioOutput: takeAudio
                                onMediaStatusChanged: {
                                    if (mediaStatus === MediaPlayer.EndOfMedia) {
                                        position = 0
                                        if (index === 0)
                                            root.setTakeComparisonPlaying(false)
                                    }
                                }
                            }

                            AudioOutput {
                                id: takeAudio
                                muted: index !== 0
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: theme.space8
                                spacing: theme.space6

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: theme.radius6
                                    color: theme.mediaCanvas
                                    clip: true

                                    Image {
                                        anchors.fill: parent
                                        source: String(takeCard.modelData.thumbnailSource || "")
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        visible: takeCard.modelData.mediaType !== "video"
                                                 || takePlayer.mediaStatus < MediaPlayer.LoadedMedia
                                    }

                                    VideoOutput {
                                        id: takeVideo
                                        anchors.fill: parent
                                        fillMode: VideoOutput.PreserveAspectFit
                                        visible: takeCard.modelData.mediaType === "video"
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: String(takeCard.modelData.cameraLabel
                                                 || takeCard.modelData.assetId || "Take")
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeBody
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    Layout.fillWidth: true

                                    MenuButton {
                                        iconName: "rating"
                                        text: String(root.mediaMetadataFor(
                                                         takeCard.modelData.assetId).rating || 0)
                                        compact: true
                                        primaryOpensMenu: true
                                        menuWidth: 140
                                        menuItems: [0, 1, 2, 3, 4, 5].map(function(rating) {
                                            return {"key": String(rating),
                                                    "label": rating === 0 ? "Unrated" : rating + " star" + (rating === 1 ? "" : "s"),
                                                    "icon": rating === 0 ? "star" : "rating"}
                                        })
                                        onMenuItemTriggered: function(key) {
                                            root.documentController.setMediaAssetRating(
                                                        [String(takeCard.modelData.assetId)], Number(key))
                                        }
                                    }

                                    Item { Layout.fillWidth: true }

                                    ActionButton {
                                        objectName: "timelineChooseTakeButton"
                                        text: "Load Source"
                                        iconName: "check"
                                        tone: "primary"
                                        compact: true
                                        onClicked: {
                                            root.loadIntoSource(
                                                        String(takeCard.modelData.assetId || ""),
                                                        String(takeCard.modelData.filePath || ""),
                                                        String(takeCard.modelData.cameraLabel
                                                               || takeCard.modelData.assetId || "Take"),
                                                        String(takeCard.modelData.thumbnailSource || ""))
                                            takeComparisonPopup.close()
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

    Timer {
        id: takeComparisonSyncTimer
        interval: 80
        running: takeComparisonPopup.opened && root.takeComparisonPlaying
        repeat: true
        onTriggered: {
            const leader = takeComparisonRepeater.itemAt(0)
            if (!leader)
                return
            const position = leader.normalizedPosition
            for (let index = 1; index < takeComparisonRepeater.count; ++index) {
                const follower = takeComparisonRepeater.itemAt(index)
                if (follower && Math.abs(follower.normalizedPosition - position) > 0.025)
                    follower.seekNormalized(position)
            }
        }
    }

    AppPopup {
        id: mediaUsagePopup
        objectName: "timelineMediaUsagePopup"
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(620), Math.max(theme.scaled(390), root.width - theme.space16))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(640), Math.max(theme.scaled(430), root.height - theme.space24))))
        x: Math.max(theme.space8, (root.width - width) / 2)
        y: Math.max(theme.space12, (root.height - height) / 2)
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space10

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        text: "Media Usage"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: "Read-only analysis until Remove or Consolidate is confirmed"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                ActionButton {
                    iconName: "refresh"
                    showText: false
                    compact: true
                    tooltipText: "Refresh media usage"
                    onClicked: root.refreshMediaUsageAnalysis()
                }

                ActionButton {
                    iconName: "close"
                    showText: false
                    compact: true
                    tooltipText: "Close media usage"
                    onClicked: mediaUsagePopup.close()
                }
            }

            Flickable {
                objectName: "timelineMediaUsageFilters"
                Layout.fillWidth: true
                Layout.preferredHeight: mediaUsageFilterRow.implicitHeight
                contentWidth: mediaUsageFilterRow.implicitWidth
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Row {
                    id: mediaUsageFilterRow
                    spacing: theme.space4

                    Repeater {
                        model: [
                            {"key": "all", "label": "All",
                             "count": (root.mediaUsageAnalysis.items || []).length},
                            {"key": "used", "label": "Used",
                             "count": Number((root.mediaUsageAnalysis.summary || {}).used || 0)},
                            {"key": "unused", "label": "Unused",
                             "count": Number((root.mediaUsageAnalysis.summary || {}).unused || 0)},
                            {"key": "duplicate", "label": "Duplicates",
                             "count": Number((root.mediaUsageAnalysis.summary || {}).duplicate || 0)},
                            {"key": "offline", "label": "Offline",
                             "count": Number((root.mediaUsageAnalysis.summary || {}).offline || 0)},
                            {"key": "derivative", "label": "Derivatives",
                             "count": Number((root.mediaUsageAnalysis.summary || {}).derivative || 0)}
                        ]

                        delegate: FilterChip {
                            required property var modelData
                            text: modelData.label + " " + String(modelData.count)
                            active: root.mediaUsageFilter === modelData.key
                            onClicked: root.mediaUsageFilter = modelData.key
                        }
                    }
                }
            }

            ListView {
                objectName: "timelineMediaUsageList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: theme.space6
                model: root.mediaUsageAnalysis.items || []

                delegate: Rectangle {
                    id: usageRow
                    required property var modelData
                    readonly property bool shown: root.usageItemVisible(modelData)
                    width: ListView.view ? ListView.view.width : 400
                    height: shown ? Math.max(theme.scaled(92), usageContent.implicitHeight + theme.space16) : 0
                    visible: shown
                    radius: theme.radius8
                    color: theme.alpha(theme.ink850, 0.82)
                    border.width: root.mediaUsageCanonicalAssetId ===
                                  String(modelData.assetId || "") ? theme.chromeBorderWidth : 0
                    border.color: theme.accentCool400
                    clip: true

                    RowLayout {
                        id: usageContent
                        anchors.fill: parent
                        anchors.margins: theme.space8
                        spacing: theme.space8

                        Image {
                            Layout.preferredWidth: theme.scaled(72)
                            Layout.preferredHeight: theme.scaled(54)
                            source: String(usageRow.modelData.thumbnailSource || "")
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: theme.space3

                            Label {
                                Layout.fillWidth: true
                                text: String(usageRow.modelData.cameraLabel
                                             || usageRow.modelData.assetId || "Media")
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeBody
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: (usageRow.modelData.used === true
                                       ? String(usageRow.modelData.usedCount || 0) + " sequence use"
                                       : "Unused")
                                      + (usageRow.modelData.duplicate === true ? " · Duplicate" : "")
                                      + (usageRow.modelData.offline === true ? " · Offline" : "")
                                      + (usageRow.modelData.derivative === true ? " · Derivative" : "")
                                color: usageRow.modelData.offline === true
                                       ? theme.error : theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                ActionButton {
                                    objectName: "timelineRevealMediaUsageButton"
                                    text: "Reveal"
                                    iconName: "search"
                                    compact: true
                                    onClicked: {
                                        const result = root.documentController.revealMediaUsage(
                                                    String(usageRow.modelData.assetId || ""))
                                        if (result.timelineUsed !== true && root.assetCatalog)
                                            root.assetCatalog.selectAssetById(
                                                        String(usageRow.modelData.assetId || ""))
                                        mediaUsagePopup.close()
                                    }
                                }

                                ActionButton {
                                    objectName: "timelineChooseCanonicalMediaButton"
                                    visible: usageRow.modelData.duplicate === true
                                    text: root.mediaUsageCanonicalAssetId ===
                                          String(usageRow.modelData.assetId || "")
                                          ? "Canonical" : "Use This"
                                    iconName: "check"
                                    compact: true
                                    active: root.mediaUsageCanonicalAssetId ===
                                            String(usageRow.modelData.assetId || "")
                                    enabled: usageRow.modelData.offline !== true
                                    onClicked: root.mediaUsageCanonicalAssetId =
                                               String(usageRow.modelData.assetId || "")
                                }

                                Item { Layout.fillWidth: true }

                                ConfirmActionButton {
                                    objectName: "timelineRemoveUnusedMediaButton"
                                    visible: usageRow.modelData.unused === true
                                             && usageRow.modelData.duplicate !== true
                                    text: "Remove"
                                    confirmText: "Confirm Remove"
                                    helperText: "Removes from this project only · Undo is available"
                                    iconName: "trash"
                                    compact: true
                                    fillButtonWidth: false
                                    onConfirmed: {
                                        if (root.documentController.removeUnusedMediaFromProject(
                                                    [String(usageRow.modelData.assetId || "")]) > 0)
                                            root.refreshMediaUsageAnalysis()
                                    }
                                }

                                ConfirmActionButton {
                                    objectName: "timelineConsolidateMediaButton"
                                    visible: usageRow.modelData.duplicate === true
                                             && root.mediaUsageCanonicalAssetId ===
                                                String(usageRow.modelData.assetId || "")
                                    text: "Consolidate"
                                    confirmText: "Confirm Group"
                                    helperText: "Replaces verified uses · keeps timing · Undo is available"
                                    iconName: "layers"
                                    compact: true
                                    fillButtonWidth: false
                                    onConfirmed: root.consolidateUsageGroup(usageRow.modelData)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    AppPopup {
        id: smartCollectionsPopup
        objectName: "timelineSmartCollectionsPopup"
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(560), Math.max(theme.scaled(380), root.width - theme.space16))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(720), Math.max(theme.scaled(480), root.height - theme.space24))))
        x: Math.max(theme.space8, (root.width - width) / 2)
        y: Math.max(theme.space12, (root.height - height) / 2)
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space10

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        text: "Smart Collections"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: "Saved live filters · every configured rule must match"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                ActionButton {
                    iconName: "close"
                    showText: false
                    compact: true
                    tooltipText: "Close smart collections"
                    onClicked: smartCollectionsPopup.close()
                }
            }

            ScrollView {
                id: smartCollectionsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: smartCollectionsScroll.availableWidth
                    spacing: theme.space10

                    Label {
                        visible: root.documentController
                                 && root.documentController.smartCollections.length === 0
                        Layout.fillWidth: true
                        text: "No smart collections yet. Define only the rules you need below."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeBody
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: root.documentController
                               ? root.documentController.smartCollections : []

                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: smartCollectionRow.implicitHeight + theme.space8 * 2
                            radius: theme.radiusSurface
                            color: modelData.active === true
                                   ? theme.alpha(theme.accentCool500, 0.13)
                                   : theme.alpha(theme.ink800, 0.66)
                            border.width: 0

                            RowLayout {
                                id: smartCollectionRow
                                anchors.fill: parent
                                anchors.margins: theme.space8
                                spacing: theme.space6

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space2

                                    Label {
                                        Layout.fillWidth: true
                                        text: String(modelData.name || "Smart Collection")
                                        color: theme.textStrong
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeControl
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        text: String(modelData.ruleCount || 0) + " live rule"
                                              + (Number(modelData.ruleCount || 0) === 1 ? "" : "s")
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeCaption
                                    }
                                }

                                ActionButton {
                                    objectName: "timelineUseSmartCollectionButton"
                                    text: modelData.active === true ? "Active" : "Use"
                                    iconName: "filter"
                                    compact: true
                                    active: modelData.active === true
                                    onClicked: root.documentController.activeSmartCollectionId =
                                                   String(modelData.collectionId || "")
                                }

                                ActionButton {
                                    objectName: "timelineEditSmartCollectionButton"
                                    iconName: "edit"
                                    showText: false
                                    compact: true
                                    tooltipText: "Edit smart collection"
                                    onClicked: root.editSmartCollection(modelData)
                                }

                                ConfirmActionButton {
                                    objectName: "timelineDeleteSmartCollectionButton"
                                    text: "Delete"
                                    confirmText: "Confirm"
                                    helperText: "Undo is available"
                                    iconName: "trash"
                                    compact: true
                                    fillButtonWidth: false
                                    onConfirmed: {
                                        root.documentController.deleteSmartCollection(
                                                    String(modelData.collectionId || ""))
                                        if (root.smartCollectionEditingId ===
                                                String(modelData.collectionId || ""))
                                            root.resetSmartCollectionDraft()
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: theme.alpha(theme.textFaint, 0.16)
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: root.smartCollectionEditingId.length > 0
                                  ? "EDIT RULES" : "NEW COLLECTION"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                        }

                        ActionButton {
                            visible: root.smartCollectionEditingId.length > 0
                            text: "New"
                            iconName: "plus"
                            compact: true
                            onClicked: root.resetSmartCollectionDraft()
                        }
                    }

                    ToneTextField {
                        objectName: "timelineSmartCollectionNameField"
                        Layout.fillWidth: true
                        text: root.smartCollectionNameDraft
                        placeholderText: "Collection name"
                        selectByMouse: true
                        onTextChanged: root.smartCollectionNameDraft = text
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: theme.space8
                        rowSpacing: theme.space8

                        ToneComboBox {
                            objectName: "timelineSmartCollectionMediaTypeRule"
                            Layout.fillWidth: true
                            model: ["Any media", "Photo", "Video", "Audio"]
                            currentIndex: root.optionIndex(
                                              ["any", "photo", "video", "audio"],
                                              root.smartCollectionMediaTypeDraft)
                            onActivated: root.smartCollectionMediaTypeDraft =
                                             ["any", "photo", "video", "audio"][currentIndex]
                        }

                        ToneComboBox {
                            objectName: "timelineSmartCollectionRatingRule"
                            Layout.fillWidth: true
                            model: ["Any rating", "1+ stars", "2+ stars", "3+ stars", "4+ stars", "5 stars"]
                            currentIndex: root.smartCollectionRatingDraft
                            onActivated: root.smartCollectionRatingDraft = currentIndex
                        }

                        ToneComboBox {
                            objectName: "timelineSmartCollectionLabelRule"
                            Layout.fillWidth: true
                            model: ["Any label", "None", "Red", "Orange", "Yellow", "Green", "Blue", "Purple"]
                            currentIndex: root.optionIndex(
                                              ["any", "none", "red", "orange", "yellow", "green", "blue", "purple"],
                                              root.smartCollectionLabelDraft)
                            onActivated: root.smartCollectionLabelDraft =
                                             ["any", "none", "red", "orange", "yellow", "green", "blue", "purple"][currentIndex]
                        }

                        ToneTextField {
                            objectName: "timelineSmartCollectionTagRule"
                            Layout.fillWidth: true
                            text: root.smartCollectionTagDraft
                            placeholderText: "Exact tag"
                            selectByMouse: true
                            onTextChanged: root.smartCollectionTagDraft = text
                        }

                        ToneTextField {
                            objectName: "timelineSmartCollectionDateFromRule"
                            Layout.fillWidth: true
                            text: root.smartCollectionDateFromDraft
                            placeholderText: "Date from · YYYY-MM-DD"
                            selectByMouse: true
                            onTextChanged: root.smartCollectionDateFromDraft = text
                        }

                        ToneTextField {
                            objectName: "timelineSmartCollectionDateToRule"
                            Layout.fillWidth: true
                            text: root.smartCollectionDateToDraft
                            placeholderText: "Date to · YYYY-MM-DD"
                            selectByMouse: true
                            onTextChanged: root.smartCollectionDateToDraft = text
                        }

                        ToneTextField {
                            numericEntry: true
                            objectName: "timelineSmartCollectionMinimumDurationRule"
                            Layout.fillWidth: true
                            text: root.smartCollectionMinimumDurationDraft
                            placeholderText: "Minimum seconds"
                            selectByMouse: true
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextChanged: root.smartCollectionMinimumDurationDraft = text
                        }

                        ToneTextField {
                            numericEntry: true
                            objectName: "timelineSmartCollectionMaximumDurationRule"
                            Layout.fillWidth: true
                            text: root.smartCollectionMaximumDurationDraft
                            placeholderText: "Maximum seconds"
                            selectByMouse: true
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            onTextChanged: root.smartCollectionMaximumDurationDraft = text
                        }

                        ToneComboBox {
                            objectName: "timelineSmartCollectionOrientationRule"
                            Layout.fillWidth: true
                            model: ["Any orientation", "Landscape", "Portrait", "Square"]
                            currentIndex: root.optionIndex(
                                              ["any", "landscape", "portrait", "square"],
                                              root.smartCollectionOrientationDraft)
                            onActivated: root.smartCollectionOrientationDraft =
                                             ["any", "landscape", "portrait", "square"][currentIndex]
                        }

                        ToneComboBox {
                            objectName: "timelineSmartCollectionOnlineRule"
                            Layout.fillWidth: true
                            model: ["Online or offline", "Online", "Offline"]
                            currentIndex: root.optionIndex(
                                              ["any", "online", "offline"],
                                              root.smartCollectionOnlineDraft)
                            onActivated: root.smartCollectionOnlineDraft =
                                             ["any", "online", "offline"][currentIndex]
                        }

                        ToneComboBox {
                            objectName: "timelineSmartCollectionUsageRule"
                            Layout.fillWidth: true
                            model: ["Used or unused", "Used", "Unused"]
                            currentIndex: root.optionIndex(
                                              ["any", "used", "unused"],
                                              root.smartCollectionUsageDraft)
                            onActivated: root.smartCollectionUsageDraft =
                                             ["any", "used", "unused"][currentIndex]
                        }

                        Item { Layout.fillWidth: true; implicitHeight: 1 }

                        ToneTextField {
                            objectName: "timelineSmartCollectionCustomFieldRule"
                            Layout.fillWidth: true
                            text: root.smartCollectionCustomFieldDraft
                            placeholderText: "Custom field name"
                            selectByMouse: true
                            onTextChanged: root.smartCollectionCustomFieldDraft = text
                        }

                        ToneTextField {
                            objectName: "timelineSmartCollectionCustomValueRule"
                            Layout.fillWidth: true
                            text: root.smartCollectionCustomValueDraft
                            placeholderText: "Custom field contains"
                            selectByMouse: true
                            onTextChanged: root.smartCollectionCustomValueDraft = text
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Blank controls are ignored. Smart collections update live as ratings, tags, files, and timeline usage change."
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8

                ActionButton {
                    visible: root.documentController
                             && root.documentController.activeSmartCollectionId.length > 0
                    text: "Show All"
                    compact: true
                    onClicked: root.documentController.activeSmartCollectionId = ""
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    objectName: "timelineSaveSmartCollectionButton"
                    text: root.smartCollectionEditingId.length > 0 ? "Update" : "Save Collection"
                    iconName: "check"
                    tone: "primary"
                    compact: true
                    enabled: root.smartCollectionNameDraft.trim().length > 0
                    disabledReason: "Name the smart collection before saving it."
                    onClicked: root.saveSmartCollectionDraft()
                }
            }
        }
    }

    AppPopup {
        id: mediaBinsPopup
        objectName: "timelineMediaBinsPopup"
        modal: false
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(390), Math.max(theme.scaled(280), root.width - theme.space16))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(470), Math.max(theme.scaled(320), root.height - theme.space24))))
        x: Math.max(theme.space8, root.width - width - theme.space8)
        y: theme.scaled(116)
        padding: theme.popupPadding
        closePolicy: Popup.CloseOnEscape

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space8

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        text: "Media Bins"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: "Drag media or bins onto a folder."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                ActionButton {
                    iconName: "close"
                    showText: false
                    compact: true
                    tooltipText: "Close media bins"
                    onClicked: mediaBinsPopup.close()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                ToneTextField {
                    id: mediaBinDraftField
                    Layout.fillWidth: true
                    text: root.mediaBinDraft
                    placeholderText: root.documentController
                                     && root.documentController.activeMediaBinId.length > 0
                                     ? "New child bin" : "New bin"
                    selectByMouse: true
                    onTextChanged: root.mediaBinDraft = text
                    onAccepted: root.createMediaBin(root.documentController
                                                    ? root.documentController.activeMediaBinId : "")
                }

                ActionButton {
                    objectName: "timelineCreateMediaBinButton"
                    text: "Add"
                    iconName: "plus"
                    compact: true
                    onClicked: root.createMediaBin(root.documentController
                                                  ? root.documentController.activeMediaBinId : "")
                }
            }

            Rectangle {
                objectName: "timelineAllMediaBinDropTarget"
                Layout.fillWidth: true
                implicitHeight: theme.controlHeight
                radius: theme.radiusControl
                color: allMediaDrop.containsDrag
                       ? theme.alpha(theme.focusRing, 0.16)
                       : root.documentController
                         && root.documentController.activeMediaBinId.length === 0
                         ? theme.alpha(theme.ink800, 0.9)
                         : theme.alpha(theme.ink900, 0.76)
                border.width: 0

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: theme.space10
                    anchors.rightMargin: theme.space8

                    AppIcon { name: "folder"; color: theme.textMain }
                    Label {
                        Layout.fillWidth: true
                        text: "All Media"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeControl
                    }
                    Label {
                        text: String(root.browsableMediaCountValue)
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                TapHandler {
                    onTapped: {
                        if (!root.documentController)
                            return
                        root.documentController.activeMediaBinId = ""
                        root.documentController.activeSmartCollectionId = ""
                    }
                }

                DropArea {
                    id: allMediaDrop
                    anchors.fill: parent
                    keys: ["timeline-media", "timeline-bin"]
                    onDropped: function(drop) {
                        if (!root.documentController || !drop.source)
                            return
                        if (drop.source.assetId !== undefined)
                            root.documentController.assignMediaAssetToBin(drop.source.assetId, "")
                        else if (drop.source.binId !== undefined)
                            root.documentController.moveMediaBin(drop.source.binId, "")
                        drop.acceptProposedAction()
                    }
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: theme.space4

                    Repeater {
                        model: root.mediaBinsModel

                        delegate: Rectangle {
                            id: binCard
                            required property var modelData
                            readonly property string binId: String(modelData.binId || "")
                            readonly property bool selectedForRename: root.selectedMediaBinId === binId
                            readonly property bool activeBin: root.documentController
                                                              && String(root.documentController.activeMediaBinId || "") === binId
                            Layout.fillWidth: true
                            Layout.leftMargin: Math.min(theme.space24, Number(modelData.depth || 0) * theme.space12)
                            implicitHeight: binColumn.implicitHeight + theme.space8
                            radius: theme.radiusControl
                            color: binDrop.containsDrag
                                   ? theme.alpha(theme.focusRing, 0.18)
                                   : activeBin
                                     ? theme.alpha(theme.ink800, 0.92)
                                     : theme.alpha(theme.ink900, 0.72)
                            border.width: 0
                            Drag.active: binGripDrag.active
                            Drag.source: binCard
                            Drag.keys: ["timeline-bin"]

                            ColumnLayout {
                                id: binColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: theme.space8
                                anchors.rightMargin: theme.space6
                                spacing: theme.space4

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space4

                                    Item {
                                        Layout.preferredWidth: theme.space20
                                        Layout.preferredHeight: theme.space20

                                        AppIcon {
                                            anchors.centerIn: parent
                                            name: "density"
                                            color: binGripDrag.active ? theme.focusRing : theme.textMuted
                                            implicitWidth: theme.space12
                                            implicitHeight: theme.space12
                                        }

                                        DragHandler {
                                            id: binGripDrag
                                            target: null
                                        }

                                        HoverHandler { cursorShape: Qt.OpenHandCursor }
                                    }

                                    AppIcon { name: "folder"; color: theme.textMain }

                                    Label {
                                        Layout.fillWidth: true
                                        text: String(binCard.modelData.name || "Bin")
                                        color: theme.textStrong
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeControl
                                        elide: Text.ElideRight

                                        TapHandler {
                                            onTapped: if (root.documentController)
                                                          root.documentController.activeMediaBinId = binCard.binId
                                        }
                                    }

                                    StatusChip {
                                        text: String(binCard.modelData.assetCount || 0)
                                        tone: "neutral"
                                        compact: true
                                        minimumReadableWidth: 28
                                        maximumReadableWidth: 44
                                    }

                                    MenuButton {
                                        showText: false
                                        iconName: "more"
                                        compact: true
                                        primaryOpensMenu: true
                                        tooltipText: "Bin actions"
                                        menuWidth: 190
                                        menuItems: [
                                            {"key": "open", "label": "Show this bin", "iconName": "folder"},
                                            {"key": "child", "label": "New child bin", "iconName": "plus"},
                                            {"key": "rename", "label": "Rename", "iconName": "edit"},
                                            {"key": "root", "label": "Move to top level", "iconName": "chevron-up"},
                                            {"key": "delete", "label": "Delete bin", "iconName": "delete"}
                                        ]
                                        onMenuItemTriggered: function(key) {
                                            if (!root.documentController)
                                                return
                                            if (key === "open")
                                                root.documentController.activeMediaBinId = binCard.binId
                                            else if (key === "child")
                                                root.createMediaBin(binCard.binId)
                                            else if (key === "rename") {
                                                root.selectedMediaBinId = binCard.binId
                                                root.mediaBinRenameDraft = String(binCard.modelData.name || "")
                                            } else if (key === "root")
                                                root.documentController.moveMediaBin(binCard.binId, "")
                                            else if (key === "delete")
                                                root.documentController.deleteMediaBin(binCard.binId)
                                        }
                                    }
                                }

                                RowLayout {
                                    visible: binCard.selectedForRename
                                    Layout.fillWidth: true
                                    spacing: theme.space4

                                    ToneTextField {
                                        Layout.fillWidth: true
                                        text: root.mediaBinRenameDraft
                                        selectByMouse: true
                                        onTextChanged: root.mediaBinRenameDraft = text
                                        onAccepted: {
                                            if (root.documentController
                                                    && root.documentController.renameMediaBin(
                                                        binCard.binId,
                                                        root.mediaBinRenameDraft))
                                                root.selectedMediaBinId = ""
                                        }
                                    }

                                    ActionButton {
                                        text: "Save"
                                        compact: true
                                        enabled: root.mediaBinRenameDraft.trim().length > 0
                                        onClicked: {
                                            if (root.documentController
                                                    && root.documentController.renameMediaBin(
                                                        binCard.binId,
                                                        root.mediaBinRenameDraft))
                                                root.selectedMediaBinId = ""
                                        }
                                    }
                                }
                            }

                            DropArea {
                                id: binDrop
                                anchors.fill: parent
                                keys: ["timeline-media", "timeline-bin"]
                                onDropped: function(drop) {
                                    if (!root.documentController || !drop.source)
                                        return
                                    if (drop.source.assetId !== undefined)
                                        root.documentController.assignMediaAssetToBin(
                                                    drop.source.assetId, binCard.binId)
                                    else if (drop.source.binId !== undefined
                                             && drop.source.binId !== binCard.binId)
                                        root.documentController.moveMediaBin(
                                                    drop.source.binId, binCard.binId)
                                    drop.acceptProposedAction()
                                }
                            }
                        }
                    }

                    Label {
                        visible: !root.documentController
                                 || root.mediaBinsModel.length === 0
                        Layout.fillWidth: true
                        Layout.topMargin: theme.space12
                        text: "Create a bin, then drag media into it."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
