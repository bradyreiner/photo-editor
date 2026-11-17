pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import "components"
import "workspaces"

ApplicationWindow {
    id: window
    width: Math.min(1600, Screen.desktopAvailableWidth * 0.85)
    height: Math.min(980, Screen.desktopAvailableHeight * 0.85)
    minimumWidth: 1024
    minimumHeight: 720
    visible: true
    title: "Looksy"
    color: theme.ink950
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    palette.window: theme.ink900
    palette.windowText: theme.textMain
    palette.base: theme.fieldFill
    palette.text: theme.textMain
    palette.button: theme.ink900
    palette.buttonText: theme.textMain
    palette.highlight: theme.editorControlHover
    palette.highlightedText: theme.textStrong
    palette.mid: theme.controlEdge
    palette.disabled.text: theme.textFaint
    palette.disabled.buttonText: theme.textFaint
    palette.disabled.windowText: theme.textFaint
    property var closeCoordinator: null
    property var pendingSceneLibraryEntry: null
    function deliverPendingSceneLibraryEntry() {
        if (!pendingSceneLibraryEntry || !sceneWorkspaceLoader.item) return
        const entry = pendingSceneLibraryEntry
        pendingSceneLibraryEntry = null
        sceneWorkspaceLoader.item.openLibraryScene(entry)
    }
    function openSavedSceneDocument(entry) {
        pendingSceneLibraryEntry = entry
        window.switchToWorkspaceIndex(window.sceneWorkspaceIndex)
        window.deliverPendingSceneLibraryEntry()
    }
    property bool allowUnconfirmedExportClose: false
    onClosing: function(event) {
        if (exportJobs.batchSubmissionBusy || exportJobs.pendingAdmissionCount > 0 || (!allowUnconfirmedExportClose && exportJobs.admissionError.length > 0)) {
            event.accepted = false
            exportAdmissionCloseDialog.open()
            return
        }
        if (closeCoordinator) event.accepted = closeCoordinator.handleWindowClose()
    }

    AppPopup {
        id: exportAdmissionCloseDialog
        objectName: "exportAdmissionCloseDialog"
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(480, parent.width - 32)))
        x: (parent.width - width) / 2
        y: Math.max(16, (parent.height - height) / 2)
        padding: standardPopupPadding
        background: PopupSurface {}
        contentItem: ColumnLayout {
            spacing: 16
            Label {
                Layout.fillWidth: true
                text: "Confirming your export jobs"
                color: theme.textStrong
                font.pixelSize: 18
            }
            Label {
                Layout.fillWidth: true
                text: exportJobs.status + " Keep this window open until the catalog confirms the jobs. Closing without confirmation may lose unsaved requests; any jobs already saved in the catalog are preserved."
                color: theme.textMuted
                wrapMode: Text.WordWrap
            }
            ColumnLayout {
                Layout.fillWidth: true
                ActionButton { Layout.fillWidth: true; text: "Keep editing"; onClicked: { window.allowUnconfirmedExportClose = false; exportAdmissionCloseDialog.close() } }
                ActionButton {
                    Layout.fillWidth: true
                    text: "Retry saving"
                    iconName: "retry"
                    visible: exportJobs.admissionError.length > 0
                    enabled: !exportJobs.batchSubmissionBusy && exportJobs.pendingAdmissionCount === 0 && exportJobs.historyMutationsEnabled
                    onClicked: exportJobs.retryUnconfirmedExports()
                }
                ActionButton {
                    Layout.fillWidth: true
                    text: "Close without confirmation"
                    visible: exportJobs.admissionError.length > 0
                    enabled: !exportJobs.batchSubmissionBusy && exportJobs.pendingAdmissionCount === 0
                    onClicked: { window.allowUnconfirmedExportClose = true; exportAdmissionCloseDialog.close(); window.close() }
                }
            }
        }
    }

    Connections {
        target: exportJobs
        function onAdmissionChanged() {
            if (exportJobs.pendingAdmissionCount > 0) window.allowUnconfirmedExportClose = false
        }
    }

    EditorCloseDialog {
        parent: Overlay.overlay
        coordinator: window.closeCoordinator
    }

    Theme { id: theme }

    readonly property var componentWorkspaceController: typeof componentWorkspace !== "undefined" ? componentWorkspace : null
    readonly property bool componentWorkspaceActive: componentWorkspaceController !== null &&
        componentWorkspaceController.active === true
    property bool workspaceInitialized: false
    property int currentWorkspace: 0
    property Item workspaceFocusFallbackItem: null
    property string pendingEditInspectorTab: ""
    property string pendingTimelineRecoveryRouteId: ""
    property var pendingTimelineRecoveryWorkspace: null
    property bool libraryWorkspaceLoaded: false
    property bool carouselWorkspaceLoaded: false
    property bool editWorkspaceLoaded: false
    property bool timelineWorkspaceLoaded: false
    property bool exportWorkspaceLoaded: false
    property bool sceneWorkspaceLoaded: false
    readonly property bool sceneImmersiveActive: currentWorkspace === sceneWorkspaceIndex
        && sceneWorkspaceLoader.item !== null && sceneWorkspaceLoader.item.immersive
    property bool secondaryWorkspacesPrewarmStarted: false
    property int secondaryWorkspacePrewarmStep: 0
    property bool recentUserInput: false
    property bool workspaceTransitionActive: false
    property bool workspaceTransitionCommitDeferred: false
    property int workspaceTransitionGeneration: 0
    property int pendingWorkspaceIndex: -1
    property string pendingEditToolActivation: ""
    property double pendingSavedBoardLoadRequestId: 0
    property string pendingSavedBoardLoadDocumentId: ""
    property string pendingSavedBoardPreviousDocumentId: ""
    property string pendingSavedBoardLoadFailureDetail: ""
    property int pendingSavedBoardOriginalWorkspace: -1
    property bool pendingSavedBoardRestoreEditMode: false
    property bool pendingSavedBoardLoadIsRollback: false
    property bool pendingSavedBoardLoadWasCancelled: false
    property double qmlRouteStartedAtMs: 0
    property double qmlRouteComponentCreatedAtMs: 0
    property string qmlRouteSurface: ""
    property bool qmlRouteReadyLogged: false
    property double qmlFrameLastSampleAtMs: 0
    property int qmlFrameSampleCount: 0
    property int qmlFrameJankCount: 0
    property int qmlFrameDroppedCount: 0
    property real qmlFrameMaxIntervalMs: 0
    property bool qmlFrameCaptureEnabled: false
    property string shellTransitionTitle: ""
    property string shellTransitionDetail: ""
    property color shellTransitionAccent: theme.accentWarm400
    property string libraryWorkspaceMode: "catalog"
    property string editWorkspaceMode: "adjust"
    property bool editorUsesLocalAsset: false
    property string localEditorAssetId: ""
    property string localEditorFilePath: ""
    property string localEditorPreviewSource: ""
    property string localEditorDisplayName: ""
    property string localEditorAdjustmentPatchJson: ""
    property bool carouselPhotoEditActive: false
    property string carouselPhotoEditTargetKey: ""
    property string carouselPhotoEditDisplayName: ""
    property int carouselPhotoEditGeneration: 0
    readonly property var catalogAssetModel: catalogAssets
    readonly property bool macStyle: Qt.platform.os === "osx"
    readonly property int libraryWorkspaceIndex: 0
    readonly property int editWorkspaceIndex: 1
    // Index 2 remains a compatibility route for saved workspace preferences.
    readonly property int carouselWorkspaceIndex: 2
    property bool editBoardsActive: false
    property var canvasMediaTarget: null
    property var pendingCanvasMediaPaths: []
    property var pendingCanvasVideos: []
    property var pendingCanvasRecipeIds: []
    property bool pendingBoardSequencePlayback: false
    property bool retainBoardTimelineView: false
    function openCurrentBoardSequence(media) {
        return openBoardSequence(media, false)
    }
    function previewCurrentBoardSequence() {
        return openBoardSequence([], true)
    }
    function openBoardSequence(media, playWhenReady) {
        const workspace = editWorkspaceLoader.item
        if (!editBoardsActive || !commitActiveEditBoard()) {
            if (workspace) workspace.showEditActionFeedback(editBoardSyncError || qsTr("Finish the current edit before opening this video sequence."), true)
            return false
        }
        const board = carouselDocument.slides[carouselDocument.currentIndex] || ({})
        const session = timelineDocument.carouselBoardSession
        const alreadyOpen = session.documentId === carouselDocument.savedDocumentId && session.boardId === board.boardId
        if (!alreadyOpen && !timelineDocument.openCarouselBoardSequence(carouselDocument, carouselDocument.currentIndex, media || [])) {
            if (workspace) workspace.showEditActionFeedback(timelineDocument.status, true)
            showPhotoRecipeNotice(timelineDocument.status, false)
            return false
        }
        retainBoardTimelineView = true
        pendingBoardSequencePlayback = playWhenReady
        if (playWhenReady) timelineDocument.seekSequenceSeconds(0)
        const opened = switchToWorkspaceIndex(timelineWorkspaceIndex)
        if (!opened) pendingBoardSequencePlayback = false
        else Qt.callLater(startPendingBoardSequencePlayback)
        return opened
    }
    function startPendingBoardSequencePlayback() {
        if (!pendingBoardSequencePlayback || workspaceTransitionActive || currentWorkspace !== timelineWorkspaceIndex ||
                timelineWorkspaceLoader.status !== Loader.Ready || !timelineWorkspaceLoader.item || timelineWorkspaceLoader.item.timelineSetupOnly === true ||
                !timelinePlayback.workspaceActive || !timelinePlayback.mediaReady) return
        pendingBoardSequencePlayback = false
        timelinePlayback.play()
    }

    function beginCanvasMediaSelection() {
        if (!editBoardsActive || !commitActiveEditBoard()) return false
        if (editWorkspaceLoader.item) editWorkspaceLoader.item.boardSequencePlaying = false
        const slide = carouselDocument.slides[carouselDocument.currentIndex] || ({})
        canvasMediaTarget = { documentId: carouselDocument.savedDocumentId,
            boardId: String(slide.boardId), name: carouselDocument.documentName,
            projectName: carouselDocument.projectName }
        const library = workspaceLoader.item
        if (library) {
            library.importModeOpen = false
            if (library.libraryWorkflowController) {
                if (canvasMediaTarget.projectName) library.libraryWorkflowController.requestProject(canvasMediaTarget.projectName)
                else library.libraryWorkflowController.requestAllImages()
            }
            library.applyLibraryWorkflowFilters({projectFilter: canvasMediaTarget.projectName || "all", mediaTypeFilter: "all"})
        }
        catalogAssets.clearSelection()
        if (!switchToWorkspaceIndex(libraryWorkspaceIndex)) { canvasMediaTarget = null; return false }
        return true
    }

    function addLibrarySelectionToCanvas() {
        if (!canvasMediaTarget || carouselDocument.savedDocumentId !== canvasMediaTarget.documentId) return false
        const ids = catalogAssets.selectedAssetIds || []
        if (!ids.length) return false
        if (!catalogAssets.selectedAssetsRecipesReady) {
            pendingCanvasRecipeIds = ids.slice()
            catalogAssets.requestAssetRecipes(ids)
            return true
        }
        pendingCanvasRecipeIds = []
        const assets = ids.map(id => {
            const asset = catalogAssets.assetById(id)
            return asset.mediaType === "photo" ? catalogAssets.assetForPhotoPlacement(id) : asset
        })
        if (assets.some(asset => ["photo", "video"].indexOf(asset.mediaType) < 0 || !asset.online || !String(asset.filePath || "").length)) {
            showPhotoRecipeNotice(qsTr("Select online photos or videos to add to the canvas."), false)
            return false
        }
        const index = carouselDocument.slides.findIndex(slide => String(slide.boardId) === canvasMediaTarget.boardId)
        if (index < 0 || !activateEditBoard(index)) return false
        pendingCanvasMediaPaths = assets.filter(asset => asset.mediaType === "photo")
        pendingCanvasVideos = assets.filter(asset => asset.mediaType === "video")
        if (!switchToWorkspaceIndex(editWorkspaceIndex)) { pendingCanvasMediaPaths = []; pendingCanvasVideos = []; return false }
        Qt.callLater(finishCanvasMediaSelection)
        return true
    }

    function finishCanvasMediaSelection() {
        if (workspaceTransitionActive || currentWorkspace !== editWorkspaceIndex || (!pendingCanvasMediaPaths.length && !pendingCanvasVideos.length)) return
        const workspace = editWorkspaceLoader.item
        if (!workspace || !canvasMediaTarget || carouselDocument.savedDocumentId !== canvasMediaTarget.documentId) return
        const paths = pendingCanvasMediaPaths
        const videos = pendingCanvasVideos
        pendingCanvasMediaPaths = []
        pendingCanvasVideos = []
        let added = 0
        for (const asset of paths.slice().reverse()) {
            if (!workspace.addDroppedImageLayer(String(asset.filePath), {x: 0.5, y: 0.5}, asset)) break
            added++
        }
        let addedVideos = 0
        for (const asset of videos.slice().reverse()) {
            if (!editorPreview.addBoardVideoLayer(String(asset.filePath), String(asset.cameraLabel || ""), String(asset.assetId || ""))) break
            addedVideos++
        }
        canvasMediaTarget = null
        let layoutReady = true
        if (added || addedVideos) {
            workspace.editSequenceBackgroundMode = true
            workspace.editInspectorTab = "layers"
            workspace.setActivePhotoLayerSelection(0, false)
            const targets = workspace.boardPhotoLayoutTargets(editorPreview.photoLayerSnapshot())
            layoutReady = targets.length < 2 || workspace.reapplyBoardPhotoLayout()
            commitActiveEditBoard()
        }
        if (added !== paths.length) showPhotoRecipeNotice(qsTr("Added %1 of %2 photos. %3").arg(added).arg(paths.length).arg(editorPreview.status), false)
        if (addedVideos !== videos.length) showPhotoRecipeNotice(editorPreview.status, false)
        if (addedVideos && layoutReady) openCurrentBoardSequence([])
    }

    function cancelCanvasMediaSelection() {
        pendingCanvasRecipeIds = []
        pendingCanvasMediaPaths = []
        pendingCanvasVideos = []
        canvasMediaTarget = null
        return switchToWorkspaceIndex(editWorkspaceIndex)
    }
    property bool switchingEditBoard: false
    property string editBoardSyncError: ""
    property var editBoardImageReplacement: ({})
    property var editBoardImageReplacementHistory: []
    property var editBoardLayerSelections: ({})
    // Native history owners publish receipts; presentation labels never decide
    // whether Cmd+Z edits a photo recipe or the Board document structure.
    property var photoWorkflowUndoEntries: []
    property var photoWorkflowRedoEntries: []
    property bool replayingPhotoWorkflow: false
    property var pendingPhotoWorkflowHistory: null
    readonly property bool photoWorkspaceActive: currentWorkspace === editWorkspaceIndex
    readonly property bool boardsWorkspaceActive: currentWorkspace === editWorkspaceIndex && editBoardsActive
    property string carouselPhotoEditOriginalPatch: ""
    readonly property int timelineWorkspaceIndex: 3
    readonly property int exportWorkspaceIndex: 4
    readonly property int sceneWorkspaceIndex: 5
    readonly property bool timelineProgramFullScreenActive: currentWorkspace === timelineWorkspaceIndex
                                                              && timelineWorkspaceLoader.item !== null
                                                              && timelineWorkspaceLoader.item.fullScreenProgramActive === true
    property int pendingPreferencesDialogPage: -1
    property bool pendingKeyboardShortcutsDialogOpen: false
    property bool pendingCommandPaletteDialogOpen: false
    property bool pendingAboutDialogOpen: false
    property bool pendingFirstRunGuideOpen: false
    property bool sessionRestoreApplied: false
    property bool sessionRestoreInProgress: false
    property alias recoveryDetailsExpanded: recoverySurface.expanded
    readonly property var workspaceTitles: ["Library", "Edit", "Boards", "Timeline", "Export", "Scene"]
    readonly property var workspaceKinds: ["library", "edit", "carousel", "timeline", "export", "scene"]
    readonly property string pageVisibilityAuditContract: "page-visibility-audit:v1|pages:library,import,edit,masks,carousel,timeline,export|buckets:always,contextual,one-expansion,hover-focus,diagnostic,preserve|runtime-active:true|functionality:preserved|major-shape:preserved"
    readonly property string activityProgressPlacementContract: "activity-progress-placement:v2|header-overlay:true|layout-shift:false|text:false|cancel-action:true"
    // A full Library scene owns its activity feedback; the header is used for
    // refreshes that leave existing photos visible and for other background work.
    readonly property bool libraryLoadingSceneVisible: currentWorkspace === libraryWorkspaceIndex
        && workspaceLoader.item !== null && workspaceLoader.item.libraryLoadingSceneVisible
    readonly property bool catalogRecoveryNeeded: !catalogAssets.loading
                                                   && String(catalogAssets.errorMessage || "").length > 0
                                                   && String(catalogAssets.status || "") === "Catalog unavailable"
    readonly property bool jobRecoveryNeeded: jobQueue && String(jobQueue.startupRecoveryError || "").length > 0
    readonly property bool recoveryBannerVisible: appShell.sessionRecoveryAvailable
                                                 && !catalogRecoveryNeeded
                                                 && !jobRecoveryNeeded
                                                 && !(jobQueue && jobQueue.hasRecoveryReport)
                                                 && !appShell.catalogRepairConfirmationPending
                                                 && !appActivityErrorVisible
    readonly property bool compactRecoveryStatus: recoveryBannerVisible
        && currentWorkspace === timelineWorkspaceIndex
    readonly property bool recoverySurfaceVisible: (appShell.sessionRecoveryAvailable
                                                     || (jobQueue && jobQueue.hasRecoveryReport)
                                                     || catalogRecoveryNeeded
                                                     || jobRecoveryNeeded
                                                     || appShell.catalogRepairConfirmationPending)
                                                    && !recoveryBannerVisible
    readonly property string recoverySurfaceSummary: {
        if (catalogRecoveryNeeded)
            return "Your originals are unchanged."
        if (jobRecoveryNeeded)
            return "Background jobs are paused. You can still browse your Library; job history has not been discarded."
        if (appShell.sessionRecoveryAvailable)
            return appShell.sessionRecoverySummary
        if (jobQueue && jobQueue.hasRecoveryReport)
            return jobQueue.recoverySummary
        return appShell.catalogRecoveryStatus
    }
    readonly property var workspaceSubtitles: [
        "Browse imported assets or bring media into the catalog.",
        "Refine tone, color, crop, retouch, and local adjustments.",
        "Assemble social slides and mixed-media carousel layouts.",
        "Cut video on a dedicated editing timeline.",
        "Prepare deliveries and export jobs.",
        "Reconstruct static 3D Gaussian scenes from overlapping captures."
    ]

    readonly property var latestImportJob: {
        if (!jobQueue)
            return ({})
        jobQueue.jobCount
        jobQueue.status
        return jobQueue.latestJobForKind("catalog_import")
    }
    readonly property var latestTimelineRenderJob: {
        if (!jobQueue)
            return ({})
        jobQueue.jobCount
        jobQueue.status
        return jobQueue.latestJobForKind("timeline_render")
    }
    readonly property var latestAssetExportJob: {
        if (!jobQueue)
            return ({})
        jobQueue.jobCount
        jobQueue.status
        return jobQueue.latestJobForKind("asset_export")
    }
    readonly property bool anyAppActivity: catalogAssets.loading
                                           || catalogAssets.importing
                                           || catalogAssets.deleting
                                           || editorPreview.processing
                                           || (jobQueue && (jobQueue.activeJobCountForKind("catalog_import") > 0
                                                             || jobQueue.activeJobCountForKind("timeline_render") > 0
                                                             || jobQueue.activeJobCountForKind("asset_export") > 0))
    readonly property bool blockingActivityVisible: anyAppActivity && primaryActivityKind !== "preview_render"
    readonly property bool shellNavigationActivityVisible: workspaceTransitionActive
    readonly property bool workspaceOwnsTransferActivity: (currentWorkspace === exportWorkspaceIndex &&
                                                           (primaryActivityKind === "asset_export" || primaryActivityKind === "timeline_render" ||
                                                            (appActivityErrorVisible && !editorPreviewFailureVisible &&
                                                             (primaryFailureKind === "asset_export" || primaryFailureKind === "timeline_render"))))
                                                         || (currentWorkspace === libraryWorkspaceIndex && libraryWorkspaceMode === "import" &&
                                                             (primaryActivityKind === "catalog_import" ||
                                                              (appActivityErrorVisible && primaryFailureKind === "catalog_import")))
    readonly property bool previewUpdateVisible: editorPreview.processing
                                               && primaryActivityKind === "preview_render"
    readonly property bool editorPreviewFailureVisible: {
        const stage = String(editorPreview.previewFailureStage || "").trim()
        const detail = String(editorPreview.previewFailureDetail || "").trim()
        const status = String(editorPreview.status || "").toLowerCase()
        const failed = stage.length > 0 || detail.length > 0
                || status.indexOf("fail") >= 0
                || status.indexOf("error") >= 0
                || status.indexOf("unavailable") >= 0
                || status.indexOf("unable") >= 0
        return photoWorkspaceActive && failed
    }
    readonly property string primaryFailureKind: {
        if (!jobQueue)
            return "none"
        jobQueue.jobCount
        jobQueue.status
        if (currentWorkspace === timelineWorkspaceIndex && jobQueue.hasCurrentFailureForKind("timeline_render"))
            return "timeline_render"
        if (currentWorkspace === exportWorkspaceIndex && jobQueue.hasCurrentFailureForKind("asset_export"))
            return "asset_export"
        if (currentWorkspace === libraryWorkspaceIndex && jobQueue.hasCurrentFailureForKind("catalog_import"))
            return "catalog_import"
        if (jobQueue.hasCurrentFailureForKind("asset_export"))
            return "asset_export"
        if (jobQueue.hasCurrentFailureForKind("timeline_render"))
            return "timeline_render"
        if (jobQueue.hasCurrentFailureForKind("catalog_import"))
            return "catalog_import"
        return "none"
    }
    readonly property var latestFailureJob: {
        if (!jobQueue || primaryFailureKind === "none")
            return ({})
        jobQueue.jobCount
        jobQueue.status
        return jobQueue.latestFailedJobForKind(primaryFailureKind)
    }
    readonly property bool primaryFailureRetryable: jobQueue && jobQueue.historyMutationsEnabled
                                                 && (primaryFailureKind === "asset_export"
                                                     || primaryFailureKind === "timeline_render")
    readonly property string primaryFailureTitle: {
        if (primaryFailureKind === "asset_export")
            return "Export failed"
        if (primaryFailureKind === "timeline_render")
            return "Timeline render failed"
        if (primaryFailureKind === "catalog_import")
            return "Import failed"
        return ""
    }
    readonly property string primaryFailureDetail: {
        const error = latestFailureJob && latestFailureJob.errorMessage ? String(latestFailureJob.errorMessage) : ""
        if (error.length > 0)
            return error
        const label = latestFailureJob && latestFailureJob.displayLabel ? String(latestFailureJob.displayLabel) : ""
        return label.length > 0 ? label : "Open Export for details."
    }
    readonly property bool appActivityErrorVisible: !workspaceTransitionActive &&
                                                   (editorPreviewFailureVisible
                                                    || (!anyAppActivity && primaryFailureKind !== "none"))
    readonly property bool appActivityStatusVisible: workspaceTransitionActive || anyAppActivity || appActivityErrorVisible
    readonly property string appActivityStatusTitle: {
        if (workspaceTransitionActive)
            return shellTransitionTitle
        if (appActivityErrorVisible)
            return editorPreviewFailureVisible ? "Preview failed" : primaryFailureTitle
        if (catalogAssets.loading && catalogAssets.assetCount === 0 && catalogAssets.totalAssetCount === 0)
            return "Opening Looksy"
        return primaryActivityTitle
    }
    readonly property string appActivityStatusDetail: {
        if (workspaceTransitionActive)
            return shellTransitionDetail
        if (editorPreviewFailureVisible)
            return String(editorPreview.previewFailureDetail || editorPreview.status || "The preview could not be rendered.")
        if (appActivityErrorVisible)
            return primaryFailureDetail
        return primaryActivityDetail
    }
    readonly property bool appActivityStatusRetryable: appActivityErrorVisible &&
                                                       (editorPreviewFailureVisible || primaryFailureRetryable)
    readonly property bool appActivityStatusCancelable: !appActivityErrorVisible &&
        (pendingSavedBoardLoadRequestId > 0 || (!workspaceTransitionActive && primaryActivityCancelable))
    readonly property color primaryActivityAccent: {
        if (catalogAssets.deleting)
            return theme.error
        if (catalogAssets.loading || catalogAssets.importing)
            return theme.accentCool400
        if (editorPreview.processing)
            return theme.accentWarm400
        if (jobQueue && jobQueue.activeJobCountForKind("timeline_render") > 0)
            return theme.warning
        if (jobQueue && jobQueue.activeJobCountForKind("asset_export") > 0)
            return theme.accentCool300
        return theme.accentWarm400
    }
    readonly property string primaryActivityTitle: {
        if (catalogAssets.deleting) {
            const count = Math.max(1, Number(catalogAssets.deleteTargetCount || 0))
            return "Removing " + count + (count === 1 ? " asset" : " assets")
        }
        if (catalogAssets.loading)
            return "Refreshing library"
        if (catalogAssets.importing)
            return latestImportJob.displayLabel && latestImportJob.displayLabel.length > 0
                    ? "Importing " + latestImportJob.displayLabel
                    : "Importing media"
        if (editorPreview.processing)
            return "Rendering preview"
        if (jobQueue && jobQueue.activeJobCountForKind("timeline_render") > 0)
            return latestTimelineRenderJob.displayLabel && latestTimelineRenderJob.displayLabel.length > 0
                    ? "Rendering " + latestTimelineRenderJob.displayLabel
                    : "Rendering timeline"
        if (jobQueue && jobQueue.activeJobCountForKind("asset_export") > 0)
            return latestAssetExportJob.displayLabel && latestAssetExportJob.displayLabel.length > 0
                    ? "Exporting " + latestAssetExportJob.displayLabel
                    : "Exporting media"
        return ""
    }
    readonly property string primaryActivityDetail: {
        if (catalogAssets.deleting)
            return catalogAssets.status
        if (catalogAssets.loading)
            return catalogAssets.status
        if (catalogAssets.importing)
            return latestImportJob.status && latestImportJob.status.length > 0 ? latestImportJob.status : catalogAssets.status
        if (editorPreview.processing)
            return editorPreview.status
        if (jobQueue && jobQueue.activeJobCountForKind("timeline_render") > 0)
            return latestTimelineRenderJob.status && latestTimelineRenderJob.status.length > 0
                    ? latestTimelineRenderJob.status
                    : timelineDocument.status
        if (jobQueue && jobQueue.activeJobCountForKind("asset_export") > 0)
            return latestAssetExportJob.status && latestAssetExportJob.status.length > 0
                    ? latestAssetExportJob.status
                    : jobQueue.statusForKind("asset_export", exportJobs.status)
        return ""
    }
    readonly property real primaryActivityProgress: {
        if (catalogAssets.deleting && catalogAssets.deleteTargetCount > 0)
            return Math.max(0, Math.min(1, catalogAssets.deleteProgress / catalogAssets.deleteTargetCount))
        if (catalogAssets.loading || editorPreview.processing)
            return -1
        if (catalogAssets.importing)
            return normalizedJobProgress(latestImportJob)
        if (jobQueue && jobQueue.activeJobCountForKind("timeline_render") > 0)
            return normalizedJobProgress(latestTimelineRenderJob)
        if (jobQueue && jobQueue.activeJobCountForKind("asset_export") > 0)
            return normalizedJobProgress(latestAssetExportJob)
        return -1
    }
    readonly property string primaryActivityKind: {
        if (catalogAssets.deleting)
            return "catalog_delete"
        if (catalogAssets.loading)
            return "catalog_refresh"
        if (catalogAssets.importing)
            return "catalog_import"
        if (editorPreview.processing)
            return "preview_render"
        if (jobQueue && jobQueue.activeJobCountForKind("timeline_render") > 0)
            return "timeline_render"
        if (jobQueue && jobQueue.activeJobCountForKind("asset_export") > 0)
            return "asset_export"
        return "none"
    }
    readonly property bool primaryActivityCancelable: canCancelActivityKind(primaryActivityKind)
    function primaryShortcut(key) {
        // Qt maps its portable Ctrl modifier to Command on macOS. Using Meta
        // here binds the physical Control key while the UI advertises Command.
        return "Ctrl+" + key
    }
    function configuredShortcut(shortcutId, fallback) {
        const bindings = appShell.shortcutBindings
        let sequence = String(bindings[shortcutId] || fallback)
        // Older versions exposed "Cmd+" in the shortcut editor and persisted
        // that spelling. Preserve its intended primary modifier while mapping
        // it to Qt's portable shortcut token.
        return sequence.replace(/(^|\+)Cmd(?=\+)/g, "$1Ctrl")
    }

    function openPreferencesDialog(pageIndex) {
        pendingPreferencesDialogPage = Math.max(0, Number(pageIndex || 0))
        preferencesDialogLoader.active = true
        if (preferencesDialogLoader.item && pendingPreferencesDialogPage >= 0) {
            preferencesDialogLoader.item.openAt(pendingPreferencesDialogPage)
            pendingPreferencesDialogPage = -1
        }
    }

    function openKeyboardShortcutsDialog() {
        pendingKeyboardShortcutsDialogOpen = true
        keyboardShortcutsDialogLoader.active = true
        if (keyboardShortcutsDialogLoader.item) {
            keyboardShortcutsDialogLoader.item.open()
            pendingKeyboardShortcutsDialogOpen = false
        }
    }

    function openUiPrimitivesPage() {
        uiPrimitivesLoader.active = true
        if (uiPrimitivesLoader.item)
            uiPrimitivesLoader.item.open()
    }

    function openCommandPaletteDialog() {
        pendingCommandPaletteDialogOpen = true
        commandPaletteDialogLoader.active = true
        if (commandPaletteDialogLoader.item) {
            commandPaletteDialogLoader.item.open()
            pendingCommandPaletteDialogOpen = false
        }
    }

    function openAboutDialog() {
        pendingAboutDialogOpen = true
        aboutDialogLoader.active = true
        if (aboutDialogLoader.item) {
            aboutDialogLoader.item.open()
            pendingAboutDialogOpen = false
        }
    }

    function primaryShiftShortcut(key) {
        return "Ctrl+Shift+" + key
    }

    function primaryAltShortcut(key) {
        return "Ctrl+Alt+" + key
    }

    function coreWorkflowShortcutSignature() {
        return [
            "adjust:" + adjustToolAction.shortcut,
            "crop:" + cropToolAction.shortcut,
            "masks:" + masksToolAction.shortcut,
            "brush:" + brushToolAction.shortcut,
            "compare:" + compareToolAction.shortcut,
            "export:" + exportToolAction.shortcut
        ].join("|")
    }

    function workspaceAccent(index) {
        return theme.interaction400
    }

    function normalizedJobProgress(job) {
        const status = job && job.status ? String(job.status) : ""
        const raw = job && job.progress !== undefined ? Number(job.progress) : 0
        if (status === "Queued")
            return 0.08
        if (raw > 0)
            return Math.max(0.05, Math.min(1, raw / 100.0))
        return -1
    }

    function supportsLongOperationCancel(kind) {
        const normalizedKind = String(kind || "")
        return longOperationCancellationMode(normalizedKind, "Queued") !== "unsupported"
                || longOperationCancellationMode(normalizedKind, "Running") === "cooperative"
    }

    function cancellableActivityKinds() {
        return ["catalog_import", "timeline_render", "asset_export", "ai_model_inference"]
    }

    function knownLongOperationKind(kind) {
        return cancellableActivityKinds().indexOf(String(kind || "")) >= 0
    }

    function longOperationCancellationMode(kind, status) {
        const normalizedKind = String(kind || "")
        const normalizedStatus = String(status || "")
        if (!knownLongOperationKind(normalizedKind))
            return "unsupported"
        if (normalizedStatus === "Queued")
            return "queued-drop"
        if (normalizedStatus === "Running"
                && (normalizedKind === "asset_export" || normalizedKind === "timeline_render"))
            return "cooperative"
        if (normalizedStatus === "Canceling")
            return "already-canceling"
        return "unsupported"
    }

    function longOperationCancellationPolicySignature(kind, status) {
        const normalizedKind = String(kind || "")
        const normalizedStatus = String(status || "")
        const mode = longOperationCancellationMode(normalizedKind, normalizedStatus)
        const cancelable = mode === "queued-drop" || mode === "cooperative"
        return normalizedKind + "|" + normalizedStatus
                + "|cancel:" + String(cancelable)
                + "|mode:" + mode
    }

    function canCancelActivityKind(kind) {
        if (!jobQueue)
            return false
        const normalizedKind = String(kind || "")
        if (!supportsLongOperationCancel(normalizedKind))
            return false
        const latestJob = jobQueue.latestJobForKind(normalizedKind)
        const jobId = latestJob && latestJob.jobId ? String(latestJob.jobId) : ""
        if (jobId.length === 0)
            return false
        if (jobQueue.canCancelJob)
            return jobQueue.canCancelJob(jobId)
        const status = latestJob && latestJob.status ? String(latestJob.status) : ""
        const mode = longOperationCancellationMode(normalizedKind, status)
        return mode === "queued-drop" || mode === "cooperative"
    }

    function primaryActivityControlSignature() {
        return primaryActivityKind + "|" + String(primaryActivityProgress) + "|" + String(primaryActivityCancelable)
    }

    function activityPresentationSignature(kind, progress, cancelable, failedCount) {
        const normalizedKind = String(kind || "none")
        const blocking = normalizedKind !== "none" && normalizedKind !== "preview_render"
        const retryable = failedCount > 0 && (normalizedKind === "asset_export" || normalizedKind === "timeline_render")
        const boundedProgress = Number(progress)
        const progressState = boundedProgress >= 0 ? Math.max(0, Math.min(1, boundedProgress)).toFixed(3) : "indeterminate"
        return normalizedKind
                + "|blocking:" + String(blocking)
                + "|progress:" + progressState
                + "|cancel:" + String(Boolean(cancelable) && blocking)
                + "|retry:" + String(retryable)
    }

    function cancelJobForKind(kind) {
        if (!jobQueue || !canCancelActivityKind(kind))
            return false
        const latestJob = jobQueue.latestJobForKind(kind)
        const jobId = latestJob && latestJob.jobId ? String(latestJob.jobId) : ""
        if (jobId.length === 0)
            return false
        return jobQueue.cancelJob(jobId)
    }

    function cancelPrimaryActivity() {
        if (pendingSavedBoardLoadRequestId > 0)
            return cancelPendingSavedBoardOpen()
        return cancelJobForKind(primaryActivityKind)
    }

    function retryPrimaryFailure() {
        if (!jobQueue || !primaryFailureRetryable)
            return false
        const jobId = latestFailureJob && latestFailureJob.jobId ? String(latestFailureJob.jobId) : ""
        if (jobId.length === 0)
            return false
        return jobQueue.retryJob(jobId)
    }

    function handleAppActivityStatusAction() {
        if (editorPreviewFailureVisible)
            return editorPreview.retryThumbnailRender()
        if (primaryFailureRetryable)
            return retryPrimaryFailure()
        return cancelPrimaryActivity()
    }

    function workspaceBadgeCount(index) {
        if (!jobQueue)
            return 0

        if (index === libraryWorkspaceIndex)
            return jobQueue.activeJobCountForKind("catalog_import")
                    + (jobQueue.hasCurrentFailureForKind("catalog_import") ? 1 : 0)

        if (index === timelineWorkspaceIndex)
            return jobQueue.activeJobCountForKind("timeline_render")
                    + (jobQueue.hasCurrentFailureForKind("timeline_render") ? 1 : 0)

        if (index === exportWorkspaceIndex)
            return jobQueue.activeJobCount
                    + (jobQueue.hasCurrentFailureForKind("asset_export") ? 1 : 0)

        return 0
    }

    function workspaceBadgeText(index) {
        const count = workspaceBadgeCount(index)
        return count > 0 ? String(count) : ""
    }

    function workspaceBadgeEmphasis(index) {
        if (!jobQueue)
            return false

        if (index === libraryWorkspaceIndex)
            return jobQueue.hasCurrentFailureForKind("catalog_import")

        if (index === timelineWorkspaceIndex)
            return jobQueue.hasCurrentFailureForKind("timeline_render")

        if (index === exportWorkspaceIndex)
            return jobQueue.hasCurrentFailureForKind("asset_export")

        return false
    }

    function activeWorkspaceItem() {
        if (currentWorkspace === sceneWorkspaceIndex)
            return sceneWorkspaceLoader.item
        if (photoWorkspaceActive)
            return editWorkspaceLoader.item ? editWorkspaceLoader.item : null
        if (boardsWorkspaceActive)
            return editWorkspaceLoader.item
        if (currentWorkspace === timelineWorkspaceIndex)
            return timelineWorkspaceLoader.item ? timelineWorkspaceLoader.item : null
        if (currentWorkspace === exportWorkspaceIndex)
            return exportWorkspaceLoader.item ? exportWorkspaceLoader.item : null
        return workspaceLoader.item ? workspaceLoader.item : null
    }

    function activePhotoEditController() {
        if (window.componentWorkspaceActive && window.componentWorkspaceController.controller)
            return window.componentWorkspaceController.controller
        return editorPreview
    }

    function workspaceLoaded(index) {
        if (index === sceneWorkspaceIndex)
            return sceneWorkspaceLoaded
        if (index === libraryWorkspaceIndex)
            return libraryWorkspaceLoaded
        if (index === editWorkspaceIndex)
            return editWorkspaceLoaded
        if (index === carouselWorkspaceIndex)
            return editWorkspaceLoaded
        if (index === timelineWorkspaceIndex)
            return timelineWorkspaceLoaded
        if (index === exportWorkspaceIndex)
            return exportWorkspaceLoaded
        return false
    }

    function loadedWorkspaceItem(index) {
        if (index === sceneWorkspaceIndex)
            return sceneWorkspaceLoader.item
        if (index === libraryWorkspaceIndex)
            return workspaceLoader.item
        if (index === editWorkspaceIndex)
            return editWorkspaceLoader.item
        if (index === carouselWorkspaceIndex)
            return editWorkspaceLoader.item
        if (index === timelineWorkspaceIndex)
            return timelineWorkspaceLoader.item
        if (index === exportWorkspaceIndex)
            return exportWorkspaceLoader.item
        return null
    }

    function markWorkspaceLoaded(index) {
        if (index === sceneWorkspaceIndex) {
            sceneWorkspaceLoaded = true
            return
        }
        if (index === libraryWorkspaceIndex)
            libraryWorkspaceLoaded = true
        else if (index === editWorkspaceIndex)
            editWorkspaceLoaded = true
        else if (index === carouselWorkspaceIndex)
            carouselWorkspaceLoaded = true
        else if (index === timelineWorkspaceIndex)
            timelineWorkspaceLoaded = true
        else if (index === exportWorkspaceIndex)
            exportWorkspaceLoaded = true
    }

    function workspacePerformanceSurface(index, editToolName) {
        const workspaceIndex = Number(index)
        const tool = String(editToolName || "").trim().toLowerCase()
        if (workspaceIndex === libraryWorkspaceIndex && libraryWorkspaceMode === "import")
            return "import"
                if (workspaceIndex === editWorkspaceIndex && tool === "masks")
            return "masks"
        if (workspaceIndex >= 0 && workspaceIndex < workspaceKinds.length)
            return workspaceKinds[workspaceIndex]
        return "unknown"
    }

    function activePerformanceSurface() {
        return workspacePerformanceSurface(currentWorkspace, photoWorkspaceActive ? resolvedEditTool() : "")
    }

    function routeWorkflowId(surface) {
        return "qml-route:" + String(surface || "unknown")
    }

    function hasQmlPerformance() {
        return typeof qmlPerformance !== "undefined" && qmlPerformance
    }

    function qmlNowMs() {
        return hasQmlPerformance() ? qmlPerformance.nowMs() : Date.now()
    }

    function noteWorkspaceComponentCreated(index) {
        if (workspaceTransitionActive && pendingWorkspaceIndex === index)
            qmlRouteComponentCreatedAtMs = qmlNowMs()
    }

    function logQmlRouteReady(index, prewarmed) {
        if (!workspaceTransitionActive || qmlRouteReadyLogged || pendingWorkspaceIndex !== index)
            return
        if (!hasQmlPerformance())
            return
        qmlRouteReadyLogged = true
        const surface = qmlRouteSurface.length > 0 ? qmlRouteSurface : workspacePerformanceSurface(index, pendingEditToolActivation)
        qmlPerformance.routeReady(surface,
                                  index,
                                  qmlRouteStartedAtMs,
                                  qmlRouteComponentCreatedAtMs,
                                  prewarmed,
                                  routeWorkflowId(surface))
    }

    function logInstantQmlRoute(surface, index) {
        if (!hasQmlPerformance())
            return
        const startedAt = qmlNowMs()
        const route = String(surface || "unknown")
        qmlPerformance.routeStarted(route, index, routeWorkflowId(route))
        qmlPerformance.routeReady(route, index, startedAt, startedAt, true, routeWorkflowId(route))
    }

    function sampleQmlFrame(nowMs) {
        if (qmlFrameLastSampleAtMs <= 0) {
            qmlFrameLastSampleAtMs = nowMs
            return
        }
        const elapsed = Math.max(0, nowMs - qmlFrameLastSampleAtMs)
        qmlFrameLastSampleAtMs = nowMs
        if (elapsed > 1000) {
            // Preserve long visible-window gaps as the stalls they are. Treating
            // them as idle made multi-second synchronous workspace construction
            // disappear from the jank report.
            qmlFrameSampleCount += 1
            qmlFrameMaxIntervalMs = Math.max(qmlFrameMaxIntervalMs, elapsed)
            qmlFrameJankCount += 1
            qmlFrameDroppedCount += 1
            flushQmlFrameSummary()
            return
        }
        const janky = elapsed > 33
        const dropped = elapsed > 50
        qmlFrameSampleCount += 1
        qmlFrameMaxIntervalMs = Math.max(qmlFrameMaxIntervalMs, elapsed)
        if (janky)
            qmlFrameJankCount += 1
        if (dropped)
            qmlFrameDroppedCount += 1
        if (qmlFrameSampleCount >= 60)
            flushQmlFrameSummary()
    }

    function flushQmlFrameSummary() {
        if (qmlFrameSampleCount <= 0)
            return
        const surface = activePerformanceSurface()
        const hasFrameIssue = qmlFrameJankCount > 0 || qmlFrameDroppedCount > 0
        if (hasQmlPerformance() && (hasFrameIssue || qmlFrameCaptureEnabled)) {
            qmlPerformance.frameSummary(surface,
                                        currentWorkspace,
                                        qmlFrameSampleCount,
                                        qmlFrameMaxIntervalMs,
                                        qmlFrameJankCount,
                                        qmlFrameDroppedCount)
        }
        qmlFrameSampleCount = 0
        qmlFrameJankCount = 0
        qmlFrameDroppedCount = 0
        qmlFrameMaxIntervalMs = 0
    }

    function beginQmlFrameCaptureWindow() {
        flushQmlFrameSummary()
        qmlFrameLastSampleAtMs = qmlNowMs()
    }

    function editToolTitle(toolName) {
        const tool = String(toolName || "").toLowerCase()
        if (tool === "crop")
            return "Crop"
        if (tool === "masks")
            return "Masks"
        if (tool === "brush")
            return "Brush"
        if (tool === "compare")
            return "Compare"
        if (tool === "layers")
            return "Layers"
        if (tool === "retouch")
            return "Retouch"
        return "Adjust"
    }

    function beginShellTransition(targetWorkspaceIndex, title, detail, accent, editToolName, deferCommit) {
        const requestedIndex = Number(targetWorkspaceIndex)
        const workspaceIndex = requestedIndex === carouselWorkspaceIndex ? editWorkspaceIndex : requestedIndex
        if (timelineDocument.carouselBoardSession.documentId && workspaceIndex !== timelineWorkspaceIndex) {
            showPhotoRecipeNotice(qsTr("Apply the board sequence or choose Return to canvas before changing workspaces."), false)
            return false
        }
        if (workspaceIndex < 0 || workspaceIndex >= workspaceTitles.length)
            return false

        if (window.componentWorkspaceActive && workspaceIndex !== editWorkspaceIndex) {
            showPhotoRecipeNotice(qsTr("Apply or cancel component contents before changing workspaces."), false)
            return false
        }

        if (currentWorkspace === editWorkspaceIndex && workspaceIndex !== editWorkspaceIndex) {
            if (!resolvePhotoNavigation()) return false
            if (carouselPhotoEditActive && !commitActiveEditBoard()) return false
        }

        // A previous route's completion must not clear a newer destination.
        const generation = ++workspaceTransitionGeneration
        pendingWorkspaceIndex = workspaceIndex
        pendingEditToolActivation = String(editToolName || "")
        workspaceTransitionCommitDeferred = Boolean(deferCommit)
        qmlRouteSurface = workspacePerformanceSurface(workspaceIndex, pendingEditToolActivation)
        qmlRouteStartedAtMs = qmlNowMs()
        qmlRouteComponentCreatedAtMs = 0
        qmlRouteReadyLogged = false
        if (hasQmlPerformance())
            qmlPerformance.routeStarted(qmlRouteSurface, workspaceIndex, routeWorkflowId(qmlRouteSurface))
        shellTransitionTitle = String(title || ("Opening " + workspaceTitles[workspaceIndex]))
        shellTransitionDetail = String(detail || workspaceSubtitles[workspaceIndex])
        shellTransitionAccent = accent || workspaceAccent(workspaceIndex)
        workspaceTransitionActive = true
        publishForegroundWorkspaceIntent()
        if (workspaceIndex !== timelineWorkspaceIndex)
            timelinePlayback.workspaceActive = false
        // Navigation is driven by readiness, not the animation clock. QML
        // timers can wait on an otherwise idle scene's next animation tick.
        Qt.callLater(function() {
            if (window.workspaceTransitionGeneration === generation &&
                    !window.workspaceTransitionCommitDeferred)
                window.commitPendingWorkspaceTransition()
        })
        return true
    }

    function finishShellTransitionSoon() {
        if (!workspaceTransitionActive)
            return
        const generation = workspaceTransitionGeneration
        const destination = pendingWorkspaceIndex
        Qt.callLater(function() {
            if (window.workspaceTransitionActive
                    && window.workspaceTransitionGeneration === generation
                    && window.currentWorkspace === destination)
                window.completeShellTransition()
        })
    }

    function completeShellTransition() {
        workspaceTransitionGeneration += 1
        workspaceTransitionActive = false
        timelinePlayback.workspaceActive = currentWorkspace === timelineWorkspaceIndex
        pendingWorkspaceIndex = -1
        pendingEditToolActivation = ""
        workspaceTransitionCommitDeferred = false
        qmlRouteStartedAtMs = 0
        qmlRouteComponentCreatedAtMs = 0
        qmlRouteSurface = ""
        qmlRouteReadyLogged = false
        shellTransitionTitle = ""
        shellTransitionDetail = ""
    }

    function commitPendingWorkspaceTransition() {
        if (!workspaceTransitionActive || workspaceTransitionCommitDeferred) {
            return
        }

        const workspaceIndex = pendingWorkspaceIndex
        if (workspaceIndex < 0 || workspaceIndex >= workspaceTitles.length) {
            completeShellTransition()
            return
        }

        if (workspaceIndex === editWorkspaceIndex && pendingEditToolActivation.length > 0)
            editWorkspaceMode = pendingEditToolActivation

        currentWorkspace = workspaceIndex

        if (workspaceIndex === editWorkspaceIndex) {
            const generation = workspaceTransitionGeneration
            Qt.callLater(function() {
                if (window.workspaceTransitionGeneration !== generation
                        || !window.workspaceTransitionActive
                        || window.pendingWorkspaceIndex !== window.editWorkspaceIndex)
                    return
                window.applyPendingEditToolActivation()
                window.maybeFinishWorkspaceTransition(window.editWorkspaceIndex)
            })
            return
        }

        maybeFinishWorkspaceTransition(workspaceIndex)
    }

    function maybeFinishWorkspaceTransition(index) {
        if (!workspaceTransitionActive || pendingWorkspaceIndex !== index)
            return
        const workspace = loadedWorkspaceItem(index)
        if (workspace === null)
            return
        if ((index === editWorkspaceIndex || index === timelineWorkspaceIndex)
                && workspace.pageContentReady === false)
            return
        logQmlRouteReady(index, workspaceLoaded(index))
        finishShellTransitionSoon()
    }

    function applyPendingEditToolActivation() {
        if (pendingEditToolActivation.length === 0)
            return false
        const workspace = editWorkspaceLoader.item
        if (!workspace || typeof workspace.activateEditTool !== "function")
            return false
        const tool = pendingEditToolActivation
        workspace.activateEditTool(tool)
        pendingEditToolActivation = ""
        return true
    }

    function resolvedEditTool() {
        if (editWorkspaceMode === "compare")
            return "compare"
        if (editWorkspaceMode === "masks")
            return "masks"
        if (editWorkspaceMode === "crop")
            return "crop"
        if (editWorkspaceMode === "brush")
            return "brush"
        return "adjust"
    }

    function preloadEditWorkspace() {
        if (editWorkspaceLoaded)
            return true
        editWorkspaceLoaded = true
        return true
    }

    function preloadSecondaryWorkspaces() {
        if (secondaryWorkspacesPrewarmStarted)
            return true
        secondaryWorkspacesPrewarmStarted = true
        secondaryWorkspacePrewarmStep = 0
        secondaryWorkspacePrewarmTimer.restart()
        return true
    }

    function prewarmTimelineWorkspace() {
        if (timelineWorkspaceLoaded || currentWorkspace === timelineWorkspaceIndex)
            return false
        timelineWorkspaceLoaded = true
        return true
    }

    function preloadNextSecondaryWorkspace() {
        if (secondaryWorkspacePrewarmStep === 0) {
            if (!carouselWorkspaceLoaded)
                carouselWorkspaceLoaded = true
            secondaryWorkspacePrewarmStep += 1
            secondaryWorkspacePrewarmTimer.restart()
            return
        }
        if (secondaryWorkspacePrewarmStep === 1) {
            if (!timelineWorkspaceLoaded)
                timelineWorkspaceLoaded = true
            secondaryWorkspacePrewarmStep += 1
            secondaryWorkspacePrewarmTimer.restart()
            return
        }
        if (secondaryWorkspacePrewarmStep === 2) {
            if (!exportWorkspaceLoaded)
                exportWorkspaceLoaded = true
            secondaryWorkspacePrewarmStep += 1
        }
    }

    function editWorkspacePrewarmReady() {
        return editWorkspaceLoader.item !== null
    }

    function activePageKind() {
        if (currentWorkspace === libraryWorkspaceIndex)
            return libraryWorkspaceMode === "import" ? "import" : "library"
        if (photoWorkspaceActive)
            return editWorkspaceMode === "masks" || editWorkspaceMode === "brush" ? "masks" : "edit"
        if (boardsWorkspaceActive)
            return "carousel"
        if (currentWorkspace === timelineWorkspaceIndex)
            return "timeline"
        if (currentWorkspace === exportWorkspaceIndex)
            return "export"
        return "unknown"
    }

    function activeWorkspaceVisibilityContract() {
        const workspace = activeWorkspaceItem()
        if (workspace && typeof workspace.activeVisibilityContract === "function")
            return String(workspace.activeVisibilityContract())
        if (workspace && workspace.carouselVisibilityContract !== undefined)
            return String(workspace.carouselVisibilityContract)
        if (workspace && workspace.timelineVisibilityContract !== undefined)
            return String(workspace.timelineVisibilityContract)
        return ""
    }

    function pageVisibilityAuditSignature() {
        return pageVisibilityAuditContract +
               "|active:" + activePageKind() +
               "|live:" + activeWorkspaceVisibilityContract() +
               "|coverage:library/import/edit/masks/carousel/timeline/export"
    }

    function focusItemSupports(methodName) {
        const focusItem = activeFocusItem
        return focusItem && typeof focusItem[methodName] === "function"
    }

    function activeLibraryWorkspace() {
        return currentWorkspace === libraryWorkspaceIndex ? activeWorkspaceItem() : null
    }

    function activeTimelineView() {
        if (currentWorkspace !== timelineWorkspaceIndex)
            return null
        const workspace = activeWorkspaceItem()
        // `photoWorkspaceActive` is itself a binding and can still report its
        // previous value during a workspace-index change. Reject that stale
        // Studio object until the Timeline loader publishes its own item.
        if (!workspace || String(workspace.objectName || "") !== "timelineWorkspace"
                || workspace.timelineSetupOnly === true
                || typeof workspace.hasActiveTimelineRenderJobs !== "function")
            return null
        return workspace
    }

    function timelineHasActiveRenderJobs(view) {
        const candidate = view || activeTimelineView()
        return candidate && typeof candidate.hasActiveTimelineRenderJobs === "function"
                ? candidate.hasActiveTimelineRenderJobs() : false
    }

    function activeCarouselView() {
        return null
    }

    function activatePrimaryEditTool(toolName) {
        const cleanTool = String(toolName || "").trim().toLowerCase()
        if (window.componentWorkspaceActive) {
            const workspace = editWorkspaceLoader.item
            if (["adjust", "masks", "brush", "retouch"].indexOf(cleanTool) >= 0 &&
                    workspace && typeof workspace.activateEditTool === "function")
                return workspace.activateEditTool(cleanTool)
            showPhotoRecipeNotice(qsTr("Crop, comparison, presets, and export are unavailable while editing component contents."), false)
            return false
        }
        if (cleanTool === "export") {
            return beginShellTransition(exportWorkspaceIndex,
                                        "Opening Export",
                                        workspaceSubtitles[exportWorkspaceIndex],
                                        workspaceAccent(exportWorkspaceIndex),
                                        "")
        }
        if (cleanTool.length === 0)
            return false

        // Tool commands must leave text-entry focus behind so the next tool's
        // keyboard commands are not silently suppressed by a stale editor field.
        if (contentItem)
            contentItem.forceActiveFocus(Qt.ShortcutFocusReason)

        const selectionTool = cleanTool === "masks" || cleanTool === "brush" || cleanTool === "retouch"
        const editWorkspace = photoWorkspaceActive ? activeWorkspaceItem() : null
        if (selectionTool && editWorkspace &&
                editWorkspace.objectName === "editStudioWorkspace" &&
                typeof editWorkspace.activateEditTool === "function") {
            editWorkspaceMode = cleanTool
            logInstantQmlRoute(workspacePerformanceSurface(editWorkspaceIndex, cleanTool),
                               editWorkspaceIndex)
            editWorkspace.activateEditTool(cleanTool)
            return true
        }

        return beginShellTransition(editWorkspaceIndex,
                                    "Switching to " + editToolTitle(cleanTool),
                                    workspaceSubtitles[editWorkspaceIndex],
                                    workspaceAccent(editWorkspaceIndex),
                                    cleanTool)
    }

    function switchToWorkspaceIndex(index) {
        const workspaceIndex = Number(index)
        if (workspaceIndex < 0 || workspaceIndex >= workspaceTitles.length)
            return false

        if (workspaceIndex === currentWorkspace && workspaceLoaded(workspaceIndex) && loadedWorkspaceItem(workspaceIndex)) {
            // Clicking the current page also cancels a not-yet-committed route.
            if (workspaceTransitionActive)
                completeShellTransition()
            return true
        }

        return beginShellTransition(workspaceIndex,
                                    "Opening " + workspaceTitles[workspaceIndex],
                                    workspaceSubtitles[workspaceIndex],
                                    workspaceAccent(workspaceIndex),
                                    "")
    }

    function activateTimelineWorkspaceForAutomation() {
        return switchToWorkspaceIndex(timelineWorkspaceIndex)
    }

    function switchWorkspaceByNumber(number) {
        return switchToWorkspaceIndex(Number(number) - 1)
    }

    function openEditPresetPanelFromExport(action) {
        if (window.componentWorkspaceActive) {
            showPhotoRecipeNotice(qsTr("Presets are unavailable while editing component contents."), false)
            return false
        }
        const requestedAction = String(action || "")
        pendingEditInspectorTab = "presets"
        beginShellTransition(editWorkspaceIndex,
                             "Opening Presets",
                             workspaceSubtitles[editWorkspaceIndex],
                             workspaceAccent(editWorkspaceIndex),
                             "adjust")
        Qt.callLater(function() {
            const workspace = activeWorkspaceItem()
            if (workspace && workspace.objectName === "editStudioWorkspace") {
                workspace.editInspectorTab = "presets"
                workspace.studioPresetSearchText = ""
                if (requestedAction === "save-current" && workspace.studioPresetDraftName.length === 0)
                    workspace.studioPresetDraftName = currentSelectionLabel()
                pendingEditInspectorTab = ""
            }
        })
        return true
    }

    function restorePrimaryStudioPanels(workspace, preserveEditSidebarState) {
        if (!workspace)
            return false
        const objectName = String(workspace.objectName || "")
        const pageName = String(workspace.page || "")
        const isEditWorkspace = pageName === "edit" || objectName === "editStudioWorkspace" || objectName === "editStudioWorkspaceContent"
        if (workspace.librarySourcesPanelVisible !== undefined)
            workspace.librarySourcesPanelVisible = true
        if (workspace.libraryInspectorPanelVisible !== undefined)
            workspace.libraryInspectorPanelVisible = true
        if (workspace.editSidebarCollapsed !== undefined && preserveEditSidebarState !== true)
            workspace.editSidebarCollapsed = isEditWorkspace
        if (workspace.editContextPanelVisible !== undefined)
            workspace.editContextPanelVisible = true
        if (workspace.editInspectorPanelVisible !== undefined)
            workspace.editInspectorPanelVisible = true
        if (workspace.fullScreenEditingEnabled !== undefined)
            workspace.fullScreenEditingEnabled = false
        if (workspace.focusEditLayoutEnabled !== undefined)
            workspace.focusEditLayoutEnabled = false
        return true
    }

    function canSelectAllCurrentWorkspace() {
        if (focusItemSupports("selectAll"))
            return true

        const libraryWorkspace = activeLibraryWorkspace()
        if (libraryWorkspace && libraryWorkspace.canSelectAll)
            return libraryWorkspace.canSelectAll()

        if (currentWorkspace === timelineWorkspaceIndex)
            return timelineDocument.clipCount > 0

        return false
    }

    function selectAllCurrentWorkspace() {
        if (focusItemSupports("selectAll")) {
            activeFocusItem.selectAll()
            return
        }

        const libraryWorkspace = activeLibraryWorkspace()
        if (libraryWorkspace && libraryWorkspace.selectAll) {
            libraryWorkspace.selectAll()
            return
        }

        if (currentWorkspace === timelineWorkspaceIndex)
            timelineDocument.selectAllClips()
    }

    function canDeselectCurrentWorkspace() {
        if (focusItemSupports("deselect"))
            return true

        const libraryWorkspace = activeLibraryWorkspace()
        if (libraryWorkspace && libraryWorkspace.canDeselect)
            return libraryWorkspace.canDeselect()

        if (currentWorkspace === timelineWorkspaceIndex)
            return timelineDocument.selectedClipCount > 0

        return false
    }

    function deselectCurrentWorkspace() {
        if (focusItemSupports("deselect")) {
            activeFocusItem.deselect()
            return
        }

        const libraryWorkspace = activeLibraryWorkspace()
        if (libraryWorkspace && libraryWorkspace.deselectAll) {
            libraryWorkspace.deselectAll()
            return
        }

        if (currentWorkspace === timelineWorkspaceIndex)
            timelineDocument.deselectAllClips()
    }

    function fileNameFromPath(path) {
        if (!path || path.length === 0)
            return ""
        const segments = path.split("/")
        return segments.length > 0 ? segments[segments.length - 1] : path
    }

    function baseNameFromPath(path) {
        const fileName = fileNameFromPath(path)
        const dotIndex = fileName.lastIndexOf(".")
        return dotIndex > 0 ? fileName.slice(0, dotIndex) : fileName
    }

    readonly property bool libraryBoardsActive: currentWorkspace === libraryWorkspaceIndex && workspaceLoader.item !== null && workspaceLoader.item.libraryBoardsOpen
    readonly property bool libraryDocumentsActive: currentWorkspace === libraryWorkspaceIndex && workspaceLoader.item !== null && workspaceLoader.item.libraryDocumentsOpen
    readonly property bool photoRecipeWorkspace: (currentWorkspace === libraryWorkspaceIndex && !libraryDocumentsActive)
                                                || photoWorkspaceActive
    property bool applyingPhotoRecipe: false
    property string photoRecipeNoticeText: ""
    property bool photoRecipeNoticeUndo: false

    function canCopyPhotoRecipe() {
        return !window.componentWorkspaceActive && photoRecipeWorkspace && !copyPhotoEditsDialog.visible
                && !focusItemSupports("copy")
                && !(photoWorkspaceActive && editorUsesLocalAsset)
                && (photoWorkspaceActive ? editorPreview.hasAsset
                    : catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedMediaType === "photo"
                      && catalogAssets.selectedRecipeReady)
    }

    function canPastePhotoRecipe() {
        return !window.componentWorkspaceActive && photoRecipeWorkspace && !copyPhotoEditsDialog.visible
                && !focusItemSupports("paste")
                && !(photoWorkspaceActive && editorUsesLocalAsset)
                && (catalogAssets.selectedAssetCount > 0
                    || (catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedMediaType === "photo"))
                && (catalogAssets.selectedAssetCount > 0
                    ? catalogAssets.selectedAssetsRecipesReady : catalogAssets.selectedRecipeReady)
                && editorPreview.hasCopiedPhotoRecipe
    }

    function showPhotoRecipeNotice(text, undoAvailable) {
        photoRecipeNoticeText = text
        photoRecipeNoticeUndo = undoAvailable
        photoRecipeNotice.open()
        photoRecipeNoticeTimer.restart()
    }

    function openCopyPhotoEdits() {
        if (!canCopyPhotoRecipe()) return
        copyPhotoEditsDialog.focusReturnItem = activeFocusItem
        // Freeze the source before showing the chooser, including unsaved edits.
        copyPhotoEditsDialog.sourcePatch = photoWorkspaceActive && editorPreview.hasAsset
                ? editorPreview.currentAssetBoundAdjustmentPatchJson()
                : catalogAssets.selectedAdjustmentPatchJson
        copyPhotoEditsDialog.sourceLabel = currentSelectionLabel()
        copyPhotoEditsDialog.errorText = ""
        copyPhotoEditsDialog.open()
    }

    function openShootMatchDialog() {
        if (window.componentWorkspaceActive || (window.photoWorkspaceActive && window.editBoardsActive) || catalogAssets.selectedAssetCount < 2) return;
        if (!window.syncEditorRecipeToCatalog()) return;
        shootMatchDialog.assetIds = catalogAssets.selectedAssetIds.slice();
        shootMatchDialog.referenceId = catalogAssets.selectedAssetId;
        shootMatchDialog.open();
    }

    function pastePhotoEdits() {
        if (!canPastePhotoRecipe()) {
            if (catalogAssets.selectedAssetCount > 0 && !catalogAssets.selectedAssetsRecipesReady)
                catalogAssets.requestAssetRecipes(catalogAssets.selectedAssetIds)
            else if (catalogAssets.selectedAssetId.length > 0 && !catalogAssets.selectedRecipeReady)
                catalogAssets.requestAssetRecipes([catalogAssets.selectedAssetId])
            return 0
        }
        const targets = catalogAssets.selectedAssetIds.slice()
        if (targets.length === 0) targets.push(catalogAssets.selectedAssetId)
        const patch = editorPreview.copiedPhotoRecipePatchJson
        applyingPhotoRecipe = true
        let count = 0
        try {
            // Merge onto the latest live edit, not an older catalog snapshot.
            if (photoWorkspaceActive) syncEditorRecipeToCatalog()
            count = catalogAssets.pasteAdjustmentPatchJsonToAssetIds(targets, patch)
        } finally {
            applyingPhotoRecipe = false
        }
        showPhotoRecipeNotice(count > 0
                ? "Pasted edits to " + count + (count === 1 ? " photo." : " photos.")
                : "No photos changed. Select photos with different edits to paste.", count > 0)
        return count
    }

    function undoPastedPhotoEdits() {
        applyingPhotoRecipe = true
        try { catalogAssets.undoBatchRecipe() }
        finally { applyingPhotoRecipe = false }
        showPhotoRecipeNotice("Pasted edits undone.", false)
    }

    function currentSelectionLabel() {
        if (currentWorkspace === sceneWorkspaceIndex) {
            const workspace = sceneWorkspaceLoader.item
            return workspace && workspace.scene.projectPath
                    ? baseNameFromPath(String(workspace.scene.projectPath)) : ""
        }
        if (editorUsesLocalAsset && photoWorkspaceActive && localEditorFilePath.length > 0)
            return localEditorDisplayName.length > 0 ? localEditorDisplayName : baseNameFromPath(localEditorFilePath)
        if (photoWorkspaceActive && editorPreview.hasAsset && openedCatalogFilePath.length > 0)
            return baseNameFromPath(openedCatalogFilePath)

        if (currentWorkspace === timelineWorkspaceIndex && timelineDocument.savedRenderPath.length > 0)
            return timelineDocument.timelineName.length > 0 ? timelineDocument.timelineName : "Untitled Timeline"

        if (catalogAssets.selectedFilePath.length > 0) {
            const baseName = baseNameFromPath(catalogAssets.selectedFilePath)
            return baseName.length > 0 ? baseName : catalogAssets.selectedAssetId
        }

        return ""
    }

    function canOpenSelectedPhotoInEdit() {
        if (window.componentWorkspaceActive)
            return false
        if (catalogAssets.selectedAssetCount > 0)
            return catalogAssets.selectedMediaType === "photo"
        return catalogAssets.firstVisiblePhotoIndex >= 0
    }

    function canOpenSelectedTimeline() {
        return !window.componentWorkspaceActive && catalogAssets.selectedMediaType === "timeline"
                && catalogAssets.selectedTimelineDocumentPath.length > 0
    }

    function canSaveTimeline() {
        return currentWorkspace === timelineWorkspaceIndex && timelineDocument.hasActiveTimeline
    }

    function canRenderTimeline() {
        return canSaveTimeline() && timelineDocument.totalDurationSeconds > 0.001
    }

    function canOpenTimelineMediaBrowser() {
        return currentWorkspace === timelineWorkspaceIndex
    }

    function canControlTimelinePlayback() {
        const view = activeTimelineView()
        return currentWorkspace === timelineWorkspaceIndex && timelineDocument.currentIndex >= 0
                && view && view.shortcutsEnabled()
    }

    function canAssignSelectedMediaToCarousel() {
        const carouselView = activeCarouselView()
        return carouselView && carouselView.gallerySelectionSupported && carouselView.gallerySelectionSupported()
    }

    function assignSelectedMediaToCarousel() {
        const carouselView = activeCarouselView()
        if (carouselView && carouselView.assignSelectedGalleryMedia)
            carouselView.assignSelectedGalleryMedia()
    }

    function currentExportPayload() {
        if (window.componentWorkspaceActive)
            return null
        if (currentWorkspace === timelineWorkspaceIndex
                && timelineDocument.savedRenderPath.length > 0
                && !timelineDocument.dirty) {
            return {
                "assetId": timelineDocument.timelineAssetId.length > 0 ? timelineDocument.timelineAssetId : timelineDocument.savedDocumentPath,
                "mediaType": "timeline",
                "displayLabel": currentSelectionLabel(),
                "sourcePath": timelineDocument.savedRenderPath,
                "editedPreviewSource": "",
                "adjustmentPatchJson": ""
            }
        }

        if (editorUsesLocalAsset && photoWorkspaceActive && localEditorFilePath.length > 0) {
            return {
                "assetId": localEditorFilePath,
                "mediaType": "photo",
                "displayLabel": currentSelectionLabel(),
                "sourcePath": localEditorFilePath,
                "editedPreviewSource": editorPreview.previewSource,
                "adjustmentPatchJson": editorPreview.currentAssetBoundAdjustmentPatchJson()
            }
        }

        if (photoWorkspaceActive && editorPreview.hasAsset && openedCatalogFilePath.length > 0) {
            return {
                "assetId": editorPreview.assetId,
                "mediaType": "photo",
                "displayLabel": currentSelectionLabel(),
                "sourcePath": openedCatalogFilePath,
                "editedPreviewSource": editorPreview.previewSource,
                "adjustmentPatchJson": editorPreview.currentAssetBoundAdjustmentPatchJson()
            }
        }

        if (catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedFilePath.length > 0) {
            if (catalogAssets.selectedMediaType === "photo" && !catalogAssets.selectedRecipeReady) {
                catalogAssets.requestAssetRecipes([catalogAssets.selectedAssetId])
                return null
            }
            return {
                "assetId": catalogAssets.selectedAssetId,
                "mediaType": catalogAssets.selectedMediaType,
                "displayLabel": currentSelectionLabel(),
                "sourcePath": catalogAssets.selectedFilePath,
                "editedPreviewSource": editorPreview.assetId === catalogAssets.selectedAssetId ? editorPreview.previewSource : "",
                "adjustmentPatchJson": catalogAssets.selectedAdjustmentPatchJson
            }
        }

        return null
    }

    function openContextualExport(invoker) {
        if (currentWorkspace === timelineWorkspaceIndex) {
            const view = activeTimelineView()
            if (!view || !canRenderTimeline())
                return false
            view.openPublish(invoker || null)
            return true
        }
        return switchToWorkspaceIndex(exportWorkspaceIndex)
    }

    function canQueueExportFromMenu() {
        if (window.componentWorkspaceActive)
            return false
        // Availability must stay cheap during selection. Materialize the
        // full edit recipe only in queueExportFromMenu, on explicit activation.
        if (currentWorkspace === timelineWorkspaceIndex) {
            const view = activeTimelineView()
            return canRenderTimeline() && !timelineDocument.timelineSaveInProgress
                    && view && !view.hasActiveTimelineRenderJobs()
        }
        if (photoWorkspaceActive) {
            if (editorUsesLocalAsset && localEditorFilePath.length > 0)
                return true
            if (editorPreview.hasAsset && openedCatalogFilePath.length > 0)
                return true
        }
        return catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedFilePath.length > 0
    }

    function queueExportFromMenu() {
        if (window.componentWorkspaceActive)
            return false
        // Resolve the authored pixels before taking a recipe snapshot or opening
        // a destination picker. The later workspace transition is too late to
        // protect a queued export from an unfinished draft.
        if (photoWorkspaceActive && !resolvePhotoNavigation()) return false
        if (photoWorkspaceActive && editBoardsActive && Object.keys(carouselDocument.boardSequence(carouselDocument.currentIndex)).length > 0) {
            if (!commitActiveEditBoard()) return false
            carouselDocument.exportCarousel()
            return true
        }
        // A local canvas is already owned by the open editor. Snapshot its
        // pixels directly instead of duplicating a large paint recipe into
        // the catalog job-admission queue.
        if (photoWorkspaceActive && editorUsesLocalAsset && localEditorFilePath.length > 0) {
            if (editorPreview.photoExportBusy) {
                window.showPhotoRecipeNotice(qsTr("Wait for the current canvas export to finish."), false)
                return false
            }
            const canvasFolder = exportJobs.pickDestinationFolder(qsTr("Choose Canvas Export Folder"))
            if (!canvasFolder.length) return false
            const canvasStem = String(currentSelectionLabel() || "Looksy canvas")
                    .replace(/[\\\/:*?"<>|]/g, "_").trim() || "Looksy canvas"
            const canvasPath = canvasFolder + "/" + canvasStem + "-" + Date.now() + ".png"
            if (!editorPreview.startFlattenedPhotoExport(canvasPath)) {
                window.showPhotoRecipeNotice(editorPreview.photoExportError || qsTr("The canvas export could not start."), false)
                return false
            }
            window.showPhotoRecipeNotice(qsTr("Exporting the current canvas as a full-resolution PNG…"), true)
            return true
        }
        if (currentWorkspace === timelineWorkspaceIndex) {
            openContextualExport(null)
            return
        }
        if (!editorUsesLocalAsset &&
                !(photoWorkspaceActive && editorPreview.hasAsset && openedCatalogFilePath.length > 0) &&
                catalogAssets.selectedMediaType === "photo" &&
                !catalogAssets.selectedRecipeReady) {
            pendingMenuExportAssetId = catalogAssets.selectedAssetId
            catalogAssets.requestAssetRecipes([pendingMenuExportAssetId])
            return
        }
        const payload = currentExportPayload()
        if (!payload)
            return

        const destinationFolder = exportJobs.pickDestinationFolder("Choose Export Folder")
        if (destinationFolder.length === 0)
            return

        const preset = payload.mediaType === "timeline" ? "Original Copy" : "Full Resolution"
        if (exportJobs.queueExport(
                    payload.assetId,
                    payload.mediaType,
                    payload.displayLabel,
                    payload.sourcePath,
                    payload.editedPreviewSource,
                    payload.adjustmentPatchJson,
                    preset,
                    destinationFolder))
            switchToWorkspaceIndex(exportWorkspaceIndex)
    }

    property string openedCatalogFilePath: ""
    property string pendingRecipeEditAssetId: ""
    property string pendingMenuExportAssetId: ""
    function resolvePhotoNavigation() {
        if (window.componentWorkspaceActive) {
            showPhotoRecipeNotice(qsTr("Apply or cancel component contents before changing documents."), false)
            return false
        }
        if (pendingPhotoWorkflowHistory) return false
        if (editorPreview.photoPaintBusy || editorPreview.brushStrokeActive) return false
        if (editorPreview.selectionEditSessionActive && editorPreview.selectionEditSessionDirty) {
            showPhotoRecipeNotice(qsTr("Finish or cancel the selection before changing documents."), false)
            return false
        }
        if (editorPreview.selectionEditSessionActive && !editorPreview.finishSelectionEditSession()) return false
        const workspace = editWorkspaceLoader.item
        return !workspace || typeof workspace.resolveTextSession !== "function" || workspace.resolveTextSession()
    }

    function resetCurrentPhotoEdits() {
        if (!resolvePhotoNavigation()) return false
        if (editorPreview.selectionEditSessionActive && !editorPreview.finishSelectionEditSession()) return false
        if (editBoardsActive) {
            editorPreview.resetAdjustments()
            return true
        }
        return editorPreview.revertPhotoToOriginal()
    }

    function loadSelectedAssetIntoEditor() {
        if (!resolvePhotoNavigation()) return false
        editorPreview.catalogRecipePersistenceEnabled = true
        if (catalogAssets.selectedAssetId.length === 0 && catalogAssets.assetCount > 0)
            catalogAssets.selectIndex(0)

        if (!syncEditorRecipeToCatalog(catalogAssets.selectedAssetId)) {
            pendingRecipeEditAssetId = catalogAssets.selectedAssetId
            return false
        }
        if (catalogAssets.selectedAssetId.length === 0 || catalogAssets.selectedMediaType !== "photo") {
            openedCatalogFilePath = ""
            editorPreview.loadAsset(
                "",
                "",
                "",
                "",
                "",
                "",
                ""
            )
            return true
        }

        if (!catalogAssets.selectedRecipeReady) {
            pendingRecipeEditAssetId = catalogAssets.selectedAssetId
            catalogAssets.requestAssetRecipes([catalogAssets.selectedAssetId])
            return false
        }

        pendingRecipeEditAssetId = ""
        openedCatalogFilePath = catalogAssets.selectedFilePath
        editorPreview.loadAsset(
            catalogAssets.selectedAssetId,
            catalogAssets.selectedFilePath,
            catalogAssets.selectedThumbnailSource,
            catalogAssets.selectedCameraLabel,
            catalogAssets.selectedCaptureDate,
            catalogAssets.selectedLensModel,
            catalogAssets.selectedAdjustmentPatchJson,
            false,
            catalogAssets.selectedCanonicalRecipePresent
        )
        return true
    }

    function loadLocalAssetIntoEditor(documentOwned) {
        editorPreview.catalogRecipePersistenceEnabled = !(carouselPhotoEditActive || documentOwned === true)
        if (localEditorFilePath.length === 0)
            return

        editorPreview.loadAsset(
            localEditorAssetId.length > 0 ? localEditorAssetId : localEditorFilePath,
            localEditorFilePath,
            localEditorPreviewSource,
            localEditorDisplayName,
            "",
            "",
            localEditorAdjustmentPatchJson
        )
    }

    function commitActiveEditBoard() {
        if (!carouselPhotoEditActive) return true
        if (editorPreview.photoPaintBusy || editorPreview.brushStrokeActive || switchingEditBoard) return false
        if (editorPreview.selectionEditSessionActive && editorPreview.selectionEditSessionDirty) {
            showPhotoRecipeNotice(qsTr("Finish or cancel the selection before leaving this Board."), false)
            return false
        }
        const workspace = editWorkspaceLoader.item
        if (workspace && typeof workspace.resolveTextSession === "function" && !workspace.resolveTextSession()) return false
        if (workspace && workspace.photoTextOverlayHandle && workspace.photoTextOverlayHandle.editing)
            workspace.photoTextOverlayHandle.finish(true)
        if (workspace) workspace.finishCreativeInspectorEditing()
        if (workspace)
            editBoardLayerSelections[localEditorAssetId] = workspace.selectedEditTargetScope() === "base-image"
                    ? -1 : workspace.activePhotoLayerIndex
        return syncBoardPhotoEdit() && carouselDocument.saveDocument()
    }

    function activateEditBoard(index) {
        if (switchingEditBoard || editorPreview.photoPaintBusy || editorPreview.brushStrokeActive) return false
        if (carouselDocument.documentArchived) return false
        if (editorPreview.selectionEditSessionActive && editorPreview.selectionEditSessionDirty) {
            showPhotoRecipeNotice(qsTr("Finish or cancel the selection before switching Boards."), false)
            return false
        }
        if (carouselPhotoEditActive && !commitActiveEditBoard()) return false
        switchingEditBoard = true
        const workspace = editWorkspaceLoader.item
        const previousPhotoEditActive = carouselPhotoEditActive
        const previousEditorAssetId = localEditorAssetId
        const previousEditorPatchJson = localEditorAdjustmentPatchJson
        if (carouselPhotoEditActive && workspace)
            editBoardLayerSelections[localEditorAssetId] = workspace.selectedEditTargetScope() === "base-image"
                    ? -1 : workspace.activePhotoLayerIndex
        const payload = carouselDocument.editBoardPayload(index)
        if (!payload || !String(payload.boardId || "").length) {
            switchingEditBoard = false
            showPhotoRecipeNotice(carouselDocument.status, false)
            return false
        }
        editBoardsActive = true
        carouselPhotoEditActive = true
        carouselPhotoEditTargetKey = String(payload.targetKey)
        carouselPhotoEditDisplayName = String(payload.displayLabel || "Board")
        editorUsesLocalAsset = true
        const previousSourcePath = localEditorFilePath
        localEditorAssetId = "board:" + carouselDocument.savedDocumentId + ":" + String(payload.imageId || payload.boardId)
        localEditorFilePath = String(payload.filePath)
        localEditorPreviewSource = String(payload.previewSource || "")
        localEditorDisplayName = carouselPhotoEditDisplayName
        localEditorAdjustmentPatchJson = String(payload.adjustmentPatchJson || "")
        editorPreview.catalogRecipePersistenceEnabled = false
        const editorAssetOrSourceChanged = editorPreview.assetId !== localEditorAssetId ||
                previousSourcePath !== localEditorFilePath
        const previousEditorStateIsCurrent = previousPhotoEditActive &&
                editorPreview.assetId === previousEditorAssetId
        let editorPatchChanged = false
        if (!editorAssetOrSourceChanged) {
            editorPatchChanged = previousEditorStateIsCurrent
                    ? previousEditorPatchJson !== localEditorAdjustmentPatchJson
                    : editorPreview.currentAssetBoundAdjustmentPatchJson() !== localEditorAdjustmentPatchJson
        }
        if (editorAssetOrSourceChanged || editorPatchChanged)
            loadLocalAssetIntoEditor(true)
        if (editorPreview.assetId !== localEditorAssetId) {
            switchingEditBoard = false
            showPhotoRecipeNotice(editorPreview.status, false)
            return false
        }
        switchingEditBoard = false
        if (workspace) {
            workspace.editInspectorTab = "layers"
            const layerIndex = editBoardLayerSelections[localEditorAssetId]
            if (layerIndex !== undefined && layerIndex >= 0 && layerIndex < editorPreview.photoLayerCount)
                workspace.setActivePhotoLayerSelection(layerIndex, false)
            else {
                workspace.selectedPhotoLayerIndices = []
                workspace.setSelectedEditTarget("base-image", -1, ({}), -1)
            }
        }
        return carouselDocument.saveDocument()
    }

    function openSavedBoards(documentId) {
        const requestedDocumentId = String(documentId || "").trim()
        if (requestedDocumentId.length === 0) return false
        if (!resolvePhotoNavigation()) return false
        const restoreEditMode = carouselPhotoEditActive
        const originalWorkspace = currentWorkspace
        if (carouselPhotoEditActive && !finishCarouselPhotoEdit(true)) return false
        if (!syncEditorRecipeToCatalog()) {
            if (restoreEditMode) activateEditBoard(carouselDocument.currentIndex)
            return false
        }
        if (carouselDocument.hasActiveCarousel &&
                (carouselDocument.documentDirty || String(carouselDocument.savedDocumentId || "").length === 0) &&
                !carouselDocument.saveDocument()) {
            if (restoreEditMode) activateEditBoard(carouselDocument.currentIndex)
            return false
        }
        if (!beginShellTransition(editWorkspaceIndex, "Opening canvas", "Loading the saved board…",
                                  workspaceAccent(editWorkspaceIndex), "layers", true)) {
            return false
        }

        pendingSavedBoardPreviousDocumentId = String(carouselDocument.savedDocumentId || "")
        pendingSavedBoardOriginalWorkspace = originalWorkspace
        pendingSavedBoardRestoreEditMode = restoreEditMode
        pendingSavedBoardLoadIsRollback = false
        pendingSavedBoardLoadWasCancelled = false
        pendingSavedBoardLoadFailureDetail = ""
        pendingSavedBoardLoadDocumentId = requestedDocumentId
        const requestId = Number(carouselDocument.loadDocumentAsync(requestedDocumentId) || 0)
        if (requestId <= 0) {
            finishSavedBoardOpenFailure(carouselDocument.status || qsTr("The saved board could not be opened."))
            return false
        }
        pendingSavedBoardLoadRequestId = requestId
        return true
    }

    function finishSavedBoardOpenFailure(message) {
        const originalWorkspace = pendingSavedBoardOriginalWorkspace
        const restoreEditMode = pendingSavedBoardRestoreEditMode
        const wasCancelled = pendingSavedBoardLoadWasCancelled
        const wasRollback = pendingSavedBoardLoadIsRollback
        pendingSavedBoardLoadRequestId = 0
        pendingSavedBoardLoadDocumentId = ""
        pendingSavedBoardPreviousDocumentId = ""
        pendingSavedBoardLoadFailureDetail = ""
        pendingSavedBoardOriginalWorkspace = -1
        pendingSavedBoardRestoreEditMode = false
        pendingSavedBoardLoadIsRollback = false
        pendingSavedBoardLoadWasCancelled = false

        if (restoreEditMode && !wasRollback && carouselDocument.hasActiveCarousel &&
                !carouselDocument.documentArchived)
            activateEditBoard(carouselDocument.currentIndex)

        if (workspaceTransitionActive) {
            workspaceTransitionCommitDeferred = false
            if (originalWorkspace === editWorkspaceIndex)
                commitPendingWorkspaceTransition()
            else
                completeShellTransition()
        }
        showPhotoRecipeNotice(wasCancelled
                              ? qsTr("Opening the board was canceled. Your previous document remains open.")
                              : String(message || qsTr("The saved board could not be opened.")), false)
    }

    function cancelPendingSavedBoardOpen() {
        const requestId = Number(pendingSavedBoardLoadRequestId || 0)
        if (requestId <= 0) return false
        pendingSavedBoardLoadWasCancelled = true
        const canceled = carouselDocument.cancelDocumentLoad(requestId)
        if (!canceled) pendingSavedBoardLoadWasCancelled = false
        return canceled
    }

    function addBoardsFromCurrentEdit() {
        if (!photoWorkspaceActive || !editorPreview.hasAsset || carouselPhotoEditActive) return false
        if (!resolvePhotoNavigation()) return false
        const path = editorUsesLocalAsset ? localEditorFilePath : openedCatalogFilePath
        const name = editorUsesLocalAsset ? localEditorDisplayName : baseNameFromPath(path)
        if (!syncEditorRecipeToCatalog()) return false
        const asset = editorUsesLocalAsset ? ({}) : catalogAssets.assetById(editorPreview.assetId)
        const origin = {kind: editorUsesLocalAsset ? "local" : "catalog", assetId: editorPreview.assetId,
            thumbnailSource: editorUsesLocalAsset ? localEditorPreviewSource : String(asset.thumbnailSource || ""),
            cameraLabel: String(asset.cameraLabel || ""), captureDate: String(asset.captureDate || ""),
            lensModel: String(asset.lensModel || "")}
        if (!carouselDocument.createBoardsFromEdit(path, name,
                editorPreview.currentAssetBoundAdjustmentPatchJson(),
                editorPreview.sourceImageWidth, editorPreview.sourceImageHeight, 1, origin)) {
            showPhotoRecipeNotice(carouselDocument.status, false)
            return false
        }
        photoWorkflowUndoEntries = photoWorkflowUndoEntries.filter(function(entry) { return entry.owner !== "board" })
        photoWorkflowRedoEntries = []
        return activateEditBoard(0)
    }

    function addEditBoard(extendCurrent) {
        if (!editBoardsActive && !addBoardsFromCurrentEdit()) return false
        if (!commitActiveEditBoard()) return false
        const outgoingSelection = editBoardLayerSelections[localEditorAssetId]
        switchingEditBoard = true
        const added = carouselDocument.addEditBoard(extendCurrent)
        switchingEditBoard = false
        if (!added) {
            showPhotoRecipeNotice(carouselDocument.status, false)
            return false
        }
        if (extendCurrent) {
            const imageId = carouselDocument.slides[carouselDocument.currentIndex].imageId
            editBoardLayerSelections["board:" + carouselDocument.savedDocumentId + ":" + imageId] = outgoingSelection
        }
        // The outgoing edit is already committed; do not retarget the new board
        // back to the old board while selecting the newly added canvas.
        carouselPhotoEditActive = false
        return activateEditBoard(carouselDocument.currentIndex)
    }

    function modifyEditBoards(operation) {
        if (!editBoardsActive || carouselDocument.exportBusy) return false
        if (operation === "earlier" || operation === "later") {
            const current = carouselDocument.slides[carouselDocument.currentIndex]
            const start = Number(current.moveSpanStart)
            const end = start + Number(current.moveSpanCount)
            if (operation === "earlier") {
                if (start <= 0) return false
                return moveEditBoard(carouselDocument.currentIndex, Number(carouselDocument.slides[start - 1].moveSpanStart))
            }
            if (end >= carouselDocument.slideCount) return false
            const next = carouselDocument.slides[end]
            return moveEditBoard(carouselDocument.currentIndex, Number(next.moveSpanStart) + Number(next.moveSpanCount))
        }
        if (!commitActiveEditBoard()) return false
        if (operation === "remove" && carouselDocument.slideCount === 1 && !photoOriginReady()) return false
        const undoBefore = photoWorkflowUndoEntries
        switchingEditBoard = true
        if (operation === "remove" && !carouselDocument.removeEditBoard()) {
            switchingEditBoard = false
            photoWorkflowUndoEntries = undoBefore
            showPhotoRecipeNotice(carouselDocument.status, false)
            return false
        }
        else if (operation === "duplicate") carouselDocument.duplicateCurrentSlide()
        else if (operation === "variant" && !carouselDocument.createBoardVariant()) {
            switchingEditBoard = false
            return false
        }
        switchingEditBoard = false
        if (carouselDocument.documentArchived) {
            if (returnToPhoto(true)) return true
            // A rejected source transition must leave the original Board open.
            replayingPhotoWorkflow = true
            carouselDocument.undo()
            carouselDocument.saveDocument()
            replayingPhotoWorkflow = false
            photoWorkflowUndoEntries = undoBefore
            return false
        }
        carouselPhotoEditActive = false
        return activateEditBoard(carouselDocument.currentIndex)
    }

    function moveEditBoard(index, insertionIndex) {
        if (!editBoardsActive || switchingEditBoard || carouselDocument.editBoardImageBusy || carouselDocument.exportBusy ||
            editorPreview.photoPaintBusy || editorPreview.brushStrokeActive || !commitActiveEditBoard()) return false
        const before = carouselDocument.slides.map(function(board) { return String(board.boardId) }).join("|")
        switchingEditBoard = true
        const saved = carouselDocument.moveEditBoard(index, insertionIndex)
        switchingEditBoard = false
        const changed = before !== carouselDocument.slides.map(function(board) { return String(board.boardId) }).join("|")
        if (changed) {
            carouselPhotoEditActive = false
            activateEditBoard(carouselDocument.currentIndex)
        }
        if (editWorkspaceLoader.item && (changed || !saved))
            editWorkspaceLoader.item.showEditActionFeedback(carouselDocument.status, !saved && changed)
        return changed
    }

    function canUndoBoardMove(redo) {
        const label = redo ? carouselDocument.redoText : carouselDocument.undoText
        return editBoardsActive && carouselPhotoEditActive && !switchingEditBoard && !carouselDocument.editBoardImageBusy &&
            !carouselDocument.exportBusy && !editorPreview.photoPaintBusy && !editorPreview.brushStrokeActive &&
            (redo ? label === "Move boards" : (label === "Move boards" || label === "Edit photo") && carouselDocument.canUndoEditBoardMove())
    }

    function undoBoardMove(redo) {
        if (!canUndoBoardMove(redo) || !commitActiveEditBoard()) return false
        switchingEditBoard = true
        if (redo) carouselDocument.redo()
        else if (!carouselDocument.undoEditBoardMove()) {
            switchingEditBoard = false
            return false
        }
        switchingEditBoard = false
        carouselPhotoEditActive = false
        return activateEditBoard(carouselDocument.currentIndex)
    }

    function canUndoCreativeBoardAction(redo) {
        const label = redo ? carouselDocument.redoText : carouselDocument.undoText
        return editBoardsActive && carouselPhotoEditActive && !switchingEditBoard && editBoardSyncError.length === 0 &&
            !carouselDocument.editBoardImageBusy && !carouselDocument.exportBusy &&
            !editorPreview.photoPaintBusy && !editorPreview.brushStrokeActive &&
            ["Create board variant", "Link photo text across boards", "Detach local photo text override"].indexOf(label) >= 0
    }

    function undoCreativeBoardAction(redo) {
        if (!canUndoCreativeBoardAction(redo) || !commitActiveEditBoard()) return false
        const previousBoardId = String(carouselDocument.slides[carouselDocument.currentIndex].boardId)
        switchingEditBoard = true
        if (redo) carouselDocument.redo()
        else carouselDocument.undo()
        switchingEditBoard = false
        if (String(carouselDocument.slides[carouselDocument.currentIndex].boardId) === previousBoardId)
            return true
        carouselPhotoEditActive = false
        return activateEditBoard(carouselDocument.currentIndex)
    }

    function performPhotoUndo(redo) {
        const textWorkspace = editWorkspaceLoader.item
        if (textWorkspace && textWorkspace.textEditSessionActive)
            return textWorkspace.undoTextSession(redo)
        if (window.componentWorkspaceActive) {
            const childController = window.componentWorkspaceController.controller
            if (!childController || window.componentWorkspaceController.busy === true || childController.professionalToolsBusy ||
                    childController.photoPaintBusy || childController.brushStrokeActive)
                return false
            if (redo) childController.redo()
            else childController.undo()
            return true
        }
        if (editorPreview.selectionEditSessionActive) {
            if (redo) editorPreview.redo()
            else editorPreview.undo()
            return true
        }
        if (pendingPhotoWorkflowHistory || editorPreview.photoPaintBusy || editorPreview.brushStrokeActive) return false
        if (carouselPhotoEditActive && !commitActiveEditBoard()) return false
        if (!carouselPhotoEditActive && !syncEditorRecipeToCatalog()) return false
        const entries = redo ? photoWorkflowRedoEntries : photoWorkflowUndoEntries
        if (!entries.length) {
            // Histories loaded before the shell receipt stream remain usable.
            if (editBoardsActive) return restoreBoardHistory(redo)
            if (redo) editorPreview.redo()
            else editorPreview.undo()
            return true
        }
        const entry = entries[entries.length - 1]
        if (!canUndoPhotoWorkflow(redo)) return false
        replayingPhotoWorkflow = true
        let accepted = false
        if (entry.owner === "board") accepted = restoreBoardHistory(redo)
        else {
            if (editorPreview.assetId !== entry.resourceId && !returnToPhoto(true)) {
                replayingPhotoWorkflow = false
                return false
            }
            pendingPhotoWorkflowHistory = {redo: redo, entry: entry,
                expectedIndex: editorPreview.editHistoryCurrentIndex + (redo ? 1 : -1)}
            if (!editorPreview.restoreEditHistoryIndex(pendingPhotoWorkflowHistory.expectedIndex)) {
                pendingPhotoWorkflowHistory = null
                replayingPhotoWorkflow = false
                return false
            }
            finishPhotoWorkflowHistory()
            return true
        }
        replayingPhotoWorkflow = false
        if (accepted) movePhotoWorkflowReceipt(redo, entry)
        return accepted
    }

    function recordPhotoWorkflowHistory(owner, resourceId, label) {
        if (replayingPhotoWorkflow || !String(resourceId).length) return
        if (owner === "photo" && (carouselPhotoEditActive || switchingEditBoard)) return
        if (owner === "board" && !editBoardsActive) return
        photoWorkflowUndoEntries = photoWorkflowUndoEntries.concat([{owner: owner,
            resourceId: String(resourceId), label: String(label)}])
        photoWorkflowRedoEntries = []
    }

    function movePhotoWorkflowReceipt(redo, entry) {
        if (redo) {
            photoWorkflowRedoEntries = photoWorkflowRedoEntries.slice(0, -1)
            photoWorkflowUndoEntries = photoWorkflowUndoEntries.concat([entry])
        } else {
            photoWorkflowUndoEntries = photoWorkflowUndoEntries.slice(0, -1)
            photoWorkflowRedoEntries = photoWorkflowRedoEntries.concat([entry])
        }
    }

    function finishPhotoWorkflowHistory() {
        const pending = pendingPhotoWorkflowHistory
        if (!pending || editorPreview.assetId !== pending.entry.resourceId ||
                editorPreview.editHistoryCurrentIndex !== pending.expectedIndex) return
        movePhotoWorkflowReceipt(pending.redo, pending.entry)
        pendingPhotoWorkflowHistory = null
        replayingPhotoWorkflow = false
    }

    function canUndoPhotoWorkflow(redo) {
        const textWorkspace = editWorkspaceLoader.item
        if (textWorkspace && textWorkspace.textEditSessionActive)
            return textWorkspace.canUndoTextSession(redo)
        if (window.componentWorkspaceActive) {
            const childController = window.componentWorkspaceController.controller
            if (!childController || window.componentWorkspaceController.busy === true || childController.professionalToolsBusy ||
                    childController.photoPaintBusy || childController.brushStrokeActive)
                return false
            return redo ? childController.canRedo : childController.canUndo
        }
        if (editorPreview.selectionEditSessionActive) return redo ? editorPreview.canRedo : editorPreview.canUndo
        if (pendingPhotoWorkflowHistory || switchingEditBoard || carouselDocument.editBoardImageBusy ||
                carouselDocument.exportBusy || editorPreview.photoPaintBusy || editorPreview.brushStrokeActive) return false
        const entries = redo ? photoWorkflowRedoEntries : photoWorkflowUndoEntries
        if (!entries.length) return editBoardsActive ? (redo ? carouselDocument.canRedo : carouselDocument.canUndo)
                : (redo ? editorPreview.canRedo : editorPreview.canUndo)
        const entry = entries[entries.length - 1]
        if (entry.owner === "board")
            return entry.resourceId === carouselDocument.savedDocumentId && (redo ? carouselDocument.canRedo : carouselDocument.canUndo)
        return (entry.resourceId === editorPreview.assetId && (redo ? editorPreview.canRedo : editorPreview.canUndo))
                || (entry.resourceId === String(carouselDocument.photoOrigin.assetId || "") && editBoardsActive)
    }

    function restoreBoardHistory(redo) {
        if (redo ? !carouselDocument.canRedo : !carouselDocument.canUndo) return false
        switchingEditBoard = true
        if (redo) carouselDocument.redo()
        else carouselDocument.undo()
        switchingEditBoard = false
        if (carouselDocument.documentArchived) {
            if (!carouselDocument.saveDocument()) {
                switchingEditBoard = true
                if (redo) carouselDocument.undo()
                else carouselDocument.redo()
                switchingEditBoard = false
                showPhotoRecipeNotice(carouselDocument.saveError, false)
                return false
            }
            return returnToPhoto(true)
        }
        carouselPhotoEditActive = false
        if (!activateEditBoard(carouselDocument.currentIndex)) return false
        loadLocalAssetIntoEditor(true)
        if (currentWorkspace !== editWorkspaceIndex) switchToWorkspaceIndex(editWorkspaceIndex)
        return editorPreview.assetId === localEditorAssetId
    }

    function photoOriginReady() {
        const origin = carouselDocument.photoOrigin
        if (!String(origin.assetId || "").length) return true // standalone collection return
        if (!origin.available) {
            showPhotoRecipeNotice(String(origin.disabledReason), false)
            return false
        }
        if (origin.kind === "catalog") {
            const asset = catalogAssets.assetById(String(origin.assetId))
            if (!asset.assetId || String(asset.filePath) !== String(origin.filePath)) {
                showPhotoRecipeNotice(qsTr("The originating photo is unavailable. Relink it before returning."), false)
                return false
            }
            if (!catalogAssets.assetRecipeReady(String(origin.assetId))) {
                catalogAssets.requestAssetRecipes([String(origin.assetId)])
                showPhotoRecipeNotice(qsTr("Loading the photo's saved edits. Try Return to photo again when ready."), false)
                return false
            }
        }
        return true
    }

    function returnToPhoto(alreadyCommitted) {
        if (!photoOriginReady()) return false
        if (!alreadyCommitted && !commitActiveEditBoard()) return false
        const workspace = editWorkspaceLoader.item
        if (workspace && typeof workspace.resolveTextSession === "function" && !workspace.resolveTextSession()) return false
        const origin = carouselDocument.photoOrigin
        if (!String(origin.assetId || "").length) {
            if (!carouselDocument.documentArchived) return false
            carouselPhotoEditActive = false
            editBoardsActive = false
            editorUsesLocalAsset = false
            switchToWorkspaceIndex(libraryWorkspaceIndex)
            Qt.callLater(function() { if (workspaceLoader.item) workspaceLoader.item.libraryCollection = "boards" })
            return true
        }
        const catalogSource = origin.kind === "catalog"
        const asset = catalogSource ? catalogAssets.assetById(String(origin.assetId)) : origin
        switchingEditBoard = true
        editorPreview.catalogRecipePersistenceEnabled = catalogSource
        editorPreview.loadAsset(String(origin.assetId), String(origin.filePath),
            String(asset.thumbnailSource || ""), String(asset.cameraLabel || origin.displayName || ""),
            String(asset.captureDate || ""), String(asset.lensModel || ""),
            String(asset.adjustmentPatchJson || origin.adjustmentPatchJson || ""), false,
            catalogSource && Boolean(asset.canonicalRecipePresent))
        if (editorPreview.assetId !== String(origin.assetId)) {
            editorPreview.catalogRecipePersistenceEnabled = false
            switchingEditBoard = false
            showPhotoRecipeNotice(editorPreview.status, false)
            return false
        }
        carouselPhotoEditActive = false
        editBoardsActive = false
        carouselPhotoEditTargetKey = ""
        carouselPhotoEditDisplayName = ""
        editorUsesLocalAsset = !catalogSource
        if (catalogSource) {
            openedCatalogFilePath = String(origin.filePath)
            catalogAssets.selectAssetById(String(origin.assetId))
        } else {
            localEditorAssetId = String(origin.assetId)
            localEditorFilePath = String(origin.filePath)
            localEditorPreviewSource = String(origin.thumbnailSource || "")
            localEditorDisplayName = String(origin.displayName || "")
            localEditorAdjustmentPatchJson = editorPreview.currentAssetBoundAdjustmentPatchJson()
        }
        switchingEditBoard = false
        return true
    }

    function requestEditBoardAction(index, operation) {
        if (operation === "add-photo") {
            if (!editBoardsActive && !addBoardsFromCurrentEdit()) return false
            if (!commitActiveEditBoard()) return false
            switchingEditBoard = true
            const added = carouselDocument.addEditPhotoBoard()
            switchingEditBoard = false
            if (!added) return false
            carouselPhotoEditActive = false
            return activateEditBoard(carouselDocument.currentIndex)
        }
        if (!editBoardsActive || switchingEditBoard || editorPreview.photoPaintBusy ||
                editorPreview.brushStrokeActive || carouselDocument.exportBusy) return false
        if (index !== carouselDocument.currentIndex && !activateEditBoard(index)) return false
        if (operation !== "rename" && operation !== "remove") return modifyEditBoards(operation)
        if (!commitActiveEditBoard()) return false
        const board = carouselDocument.slides[carouselDocument.currentIndex]
        editBoardDialog.operation = operation
        editBoardDialog.boardId = String(board.boardId)
        editBoardDialog.boardName = String(board.boardName || "Board " + (index + 1))
        editBoardDialog.errorText = ""
        editBoardNameField.text = editBoardDialog.boardName
        editBoardDialog.open()
        return true
    }

    function requestEditBoardImageDrop(index, fileUrl, operation) {
        return requestEditBoardImageDropAtPoint(index, fileUrl, operation, null)
    }

    function requestEditBoardImageDropAtPoint(index, fileUrl, operation, placement) {
        const workspace = editWorkspaceLoader.item
        if (!workspace || !editBoardsActive || switchingEditBoard || carouselDocument.editBoardImageBusy ||
                carouselDocument.exportBusy || editorPreview.photoPaintBusy || editorPreview.brushStrokeActive) return false
        if (operation !== "layer" && operation !== "replace") return false
        if (index < 0 || index >= carouselDocument.slides.length) return false
        const targetBoard = carouselDocument.slides[index] || ({})
        const activeBoard = carouselDocument.slides[carouselDocument.currentIndex] || ({})
        if (targetBoard.imageId !== activeBoard.imageId && !activateEditBoard(index)) return false
        if (operation === "layer") {
            if (!workspace.addDroppedImageLayer(fileUrl, placement)) return false
            return commitActiveEditBoard()
        }
        if (!commitActiveEditBoard()) return false
        if (!carouselDocument.replaceEditBoardImage(fileUrl)) {
            workspace.showEditActionFeedback(carouselDocument.status, true)
            return false
        }
        workspace.showEditActionFeedback("Replacing base image…", false)
        return true
    }

    function boardImageReplacement(redo) {
        const entries = window.editBoardImageReplacementHistory
        for (let i = entries.length - 1; i >= 0; --i) {
            const entry = entries[i]
            if (entry.documentId === carouselDocument.savedDocumentId && entry.assetId === localEditorAssetId &&
                localEditorFilePath === (redo ? entry.originalPath : entry.replacedPath)) return entry
        }
        return ({})
    }

    function canUndoBoardImageReplacement(redo) {
        const replacement = window.boardImageReplacement(redo)
        if (!editBoardsActive || !carouselPhotoEditActive || carouselDocument.editBoardImageBusy ||
            editorPreview.photoPaintBusy || editorPreview.brushStrokeActive ||
            replacement.documentId !== carouselDocument.savedDocumentId || replacement.assetId !== localEditorAssetId) return false
        if (redo) return carouselDocument.redoText === "Replace base image" && localEditorFilePath === replacement.originalPath
        return localEditorFilePath === replacement.replacedPath &&
            (carouselDocument.undoText === "Replace base image" ||
             (editorPreview.currentAssetBoundAdjustmentPatchJson() === replacement.baseline &&
              carouselDocument.canUndoEditBoardImageReplacement()))
    }

    function undoBoardImageReplacement(redo) {
        if (!canUndoBoardImageReplacement(redo)) return false
        switchingEditBoard = true
        if (redo) carouselDocument.redo()
        else if (!carouselDocument.undoEditBoardImageReplacement(window.boardImageReplacement(false).baseline)) {
            switchingEditBoard = false
            return false
        }
        switchingEditBoard = false
        carouselPhotoEditActive = false
        if (!activateEditBoard(carouselDocument.currentIndex)) return false
        loadLocalAssetIntoEditor(true)
        return true
    }

    function beginCarouselPhotoEdit(payload) { activateEditBoard(carouselDocument.currentIndex) }

    function syncBoardPhotoEdit() {
        if (switchingEditBoard || !carouselPhotoEditActive || editorPreview.assetId !== localEditorAssetId) return true
        const nextPatch = editorPreview.currentAssetBoundAdjustmentPatchJson()
        if (nextPatch === localEditorAdjustmentPatchJson && editBoardSyncError.length === 0) return true
        const accepted = carouselDocument.applyAdjustmentPatchToPhotoEditTarget(carouselPhotoEditTargetKey,
                    nextPatch)
        if (accepted) localEditorAdjustmentPatchJson = nextPatch
        editBoardSyncError = accepted ? "" : String(carouselDocument.status || qsTr("The board edits could not be saved."))
        if (!accepted && editWorkspaceLoader.item)
            editWorkspaceLoader.item.showEditActionFeedback(editBoardSyncError + qsTr(" Undo the last edit to return to the saved version."), true)
        return accepted
    }

    function finishCarouselPhotoEdit(applyChanges) {
        if (!carouselPhotoEditActive) return true
        if (!commitActiveEditBoard()) return false
        switchingEditBoard = true
        carouselPhotoEditActive = false
        editBoardsActive = false
        carouselPhotoEditTargetKey = ""
        carouselPhotoEditDisplayName = ""
        editorUsesLocalAsset = false
        editorPreview.catalogRecipePersistenceEnabled = true
        localEditorAssetId = ""
        localEditorFilePath = ""
        localEditorPreviewSource = ""
        localEditorDisplayName = ""
        localEditorAdjustmentPatchJson = ""
        switchingEditBoard = false
        return true
    }

    function syncEditorRecipeToCatalog(nextAssetId) {
        if (editorUsesLocalAsset)
            return true

        const currentAssetId = editorPreview.assetId
        if (currentAssetId.length === 0)
            return true

        if (nextAssetId !== undefined && nextAssetId.length > 0 && nextAssetId === currentAssetId)
            return true

        const currentCatalogAsset = catalogAssets.assetById(currentAssetId)
        if (!currentCatalogAsset.assetId) return true
        if (currentCatalogAsset.assetId && !catalogAssets.assetRecipeReady(currentAssetId)) {
            catalogAssets.requestAssetRecipes([currentAssetId])
            return false
        }

        // This is the same asset, not a reusable settings paste. Preserve its
        // layers, history and source/profile bindings as well as global edits.
        catalogAssets.applyAdjustmentPatchJsonToAssetIds(
                    [currentAssetId],
                    editorPreview.currentAssetBoundAdjustmentPatchJson())
        return true
    }

    function beginImportWorkspace() {
        if (window.componentWorkspaceActive) {
            showPhotoRecipeNotice(qsTr("Apply or cancel component contents before opening another workspace."), false)
            return false
        }
        libraryWorkspaceMode = "import"
        switchToWorkspaceIndex(libraryWorkspaceIndex)
        catalogAssets.refreshImportSources()
    }

    function openFirstRunGuide() {
        firstRunGuideOpenTimer.stop()
        if (!recoverySurfaceVisible && firstRunGuideLoader.item && firstRunGuideLoader.item.visible) {
            pendingFirstRunGuideOpen = false
            return
        }
        pendingFirstRunGuideOpen = true
        if (recoverySurfaceVisible) {
            firstRunGuideOpenTimer.restart()
            return
        }
        firstRunGuideLoader.active = true
        if (firstRunGuideLoader.item) {
            firstRunGuideLoader.item.start()
            pendingFirstRunGuideOpen = false
        }
    }

    function restoreRecoveredSession() {
        // An automation worker must select and verify its exact checkpoint
        // through MCP before loading a Timeline. Automatic session restoration
        // can otherwise open an older canonical file and start media hydration.
        if (typeof automationRequiresExplicitSessionRecovery !== "undefined"
                && automationRequiresExplicitSessionRecovery) {
            sessionRestoreApplied = true
            return true
        }
        if (sessionRestoreApplied || !appShell.sessionRecoveryAvailable)
            return true
        if (catalogAssets.loading || sessionRestoreInProgress)
            return false

        // Catalog selection, project filters and recipe application can emit
        // recipeReadinessChanged synchronously. Its handler retries recovery;
        // keep that retry from entering this restore before it has completed.
        sessionRestoreInProgress = true
        try {
            const recoveredProject = String(appShell.recoveredProject() || "")
            const recoveredAssetId = String(appShell.recoveredAssetId() || "")
            const recoveredAdjustments =
                    String(appShell.recoveredAdjustmentPatchJson() || "")
            const recoveredTimeline = String(appShell.recoveredTimelinePath() || "")
            const recoveredMode = String(appShell.recoveredEditMode() || "")

            if (recoveredProject.length > 0)
                catalogAssets.openProject(recoveredProject)
            if (recoveredAssetId.length > 0 && !catalogAssets.selectAssetById(recoveredAssetId, true))
                return false
            if (recoveredAssetId.length > 0 && !catalogAssets.assetRecipeReady(recoveredAssetId)) {
                catalogAssets.requestAssetRecipes([recoveredAssetId])
                return false
            }
            if (recoveredAssetId.length > 0
                    && recoveredAdjustments.length > 0) {
                catalogAssets.applyAdjustmentPatchJsonToAssetIds(
                            [recoveredAssetId], recoveredAdjustments)
            }
            if (recoveredTimeline.length > 0)
                timelineDocument.loadTimeline(recoveredTimeline)
            if (recoveredMode.length > 0)
                editWorkspaceMode = recoveredMode

            sessionRestoreApplied = true
            if (photoWorkspaceActive)
                loadSelectedAssetIntoEditor()
            return true
        } finally {
            // Missing assets/recipes must remain retryable on later signals.
            sessionRestoreInProgress = false
        }
    }

    function recoveryDetailItems() {
        const items = []
        if (jobRecoveryNeeded)
            items.push(String(jobQueue.startupRecoveryError))
        if (appShell.sessionRecoveryAvailable) {
            const sessionItems = appShell.sessionRecoveryDetails
            for (let index = 0; index < sessionItems.length; ++index)
                items.push(String(sessionItems[index]))
        }
        if (jobQueue && jobQueue.hasRecoveryReport) {
            const jobItems = jobQueue.recoveryDetails
            for (let index = 0; index < jobItems.length; ++index)
                items.push(String(jobItems[index]))
        }
        if (catalogRecoveryNeeded)
            items.push(String(catalogAssets.errorMessage || "Catalog startup failed."))
        if (appShell.catalogRecoveryStatus.length > 0)
            items.push(appShell.catalogRecoveryStatus)
        return items
    }

    function dismissRecoverySurface() {
        if (jobRecoveryNeeded)
            return
        if (appShell.sessionRecoveryAvailable)
            appShell.dismissSessionRecovery()
        if (jobQueue && jobQueue.hasRecoveryReport)
            jobQueue.acknowledgeRecoveryReport()
        recoveryDetailsExpanded = false
    }

    function reviewRecoveredWork() {
        recoveredWorkReviewDialog.openReview()
    }

    function openRecoveredWorkspace() {
        if (appShell.sessionRecoveryAvailable) {
            const recoveredWorkspace = appShell.recoveredWorkspace()
            appShell.dismissSessionRecovery()
            switchToWorkspaceIndex(recoveredWorkspace)
            return
        }
        if (jobRecoveryNeeded) {
            switchToWorkspaceIndex(exportWorkspaceIndex)
            return
        }
        if (jobQueue) {
            const exportJob = jobQueue.latestJobForKind("asset_export")
            if (exportJob && exportJob.metadata
                    && exportJob.metadata.recoveredAfterRestart
                    && !exportJob.metadata.recoveryAcknowledged) {
                switchToWorkspaceIndex(exportWorkspaceIndex)
                return
            }
        }
        switchToWorkspaceIndex(libraryWorkspaceIndex)
    }

    function openSelectedPhotoInEdit() {
        if (window.componentWorkspaceActive)
            return false
        if (!canOpenSelectedPhotoInEdit())
            return

        if (catalogAssets.selectedAssetCount === 0) {
            const photoIndex = catalogAssets.firstVisiblePhotoIndex
            if (photoIndex < 0)
                return
            catalogAssets.selectIndex(photoIndex)
        }

        if (carouselPhotoEditActive && !finishCarouselPhotoEdit(true)) return
        editBoardsActive = false
        carouselPhotoEditActive = false
        carouselPhotoEditTargetKey = ""
        editorUsesLocalAsset = false
        if (!loadSelectedAssetIntoEditor()) {
            pendingRecipeEditAssetId = catalogAssets.selectedAssetId
            return
        }
        pendingRecipeEditAssetId = ""
        beginShellTransition(editWorkspaceIndex,
                             "Opening Edit",
                             workspaceSubtitles[editWorkspaceIndex],
                             workspaceAccent(editWorkspaceIndex),
                             "adjust")
    }

    function openSelectedTimelineDocument() {
        if (timelineDocument.carouselBoardSession.documentId) {
            showPhotoRecipeNotice(qsTr("Apply the board sequence or return to its canvas first."), false)
            return false
        }
        if (window.componentWorkspaceActive)
            return false
        if (!canOpenSelectedTimeline())
            return

        timelineDocument.loadTimeline(catalogAssets.selectedTimelineDocumentPath)
        switchToWorkspaceIndex(timelineWorkspaceIndex)
    }

    function createTimelineFromMenu() {
        if (window.componentWorkspaceActive) {
            showPhotoRecipeNotice(qsTr("Apply or cancel component contents before creating another document."), false)
            return false
        }
        timelineDocument.createTimeline()
        switchToWorkspaceIndex(timelineWorkspaceIndex)
    }

    function createCarouselFromMenu() {
        if (window.componentWorkspaceActive) {
            showPhotoRecipeNotice(qsTr("Apply or cancel component contents before creating another document."), false)
            return false
        }
        canvasMediaTarget = null
        newCanvasDialog.resizing = false
        newCanvasDialog.projectNames = catalogAssets.projectNames
        newCanvasDialog.projectName = String(catalogAssets.projectFilter || "")
        window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
        newCanvasDialog.open()
    }

    function saveCurrentTimeline() {
        const timelineView = activeTimelineView()
        if (timelineView && timelineView.saveTimelineToLibrary)
            timelineView.saveTimelineToLibrary()
    }

    function openProjectWorkspace(destination, projectName) {
        if (window.componentWorkspaceActive) {
            showPhotoRecipeNotice(qsTr("Apply or cancel component contents before opening another document."), false)
            return false
        }
        if (carouselPhotoEditActive && !finishCarouselPhotoEdit(true)) return
        if (projectName.length === 0)
            return

        editorUsesLocalAsset = false
        catalogAssets.openProject(projectName)

        if (destination === "edit") {
            if (catalogAssets.selectedMediaType === "photo") {
                if (!loadSelectedAssetIntoEditor()) {
                    pendingRecipeEditAssetId = catalogAssets.selectedAssetId
                    return
                }
            } else {
                editorPreview.loadAsset("", "", "", "", "", "", "")
            }
            pendingRecipeEditAssetId = ""
            beginShellTransition(editWorkspaceIndex,
                                 "Opening Edit",
                                 workspaceSubtitles[editWorkspaceIndex],
                                 workspaceAccent(editWorkspaceIndex),
                                 "adjust")
            return
        }

        switchToWorkspaceIndex(timelineWorkspaceIndex)
    }

    Action {
        id: newImportAction
        text: "New Import"
        shortcut: window.photoWorkspaceActive ? "" : window.primaryShiftShortcut("I")
        onTriggered: window.beginImportWorkspace()
    }

    Action {
        id: newTimelineAction
        text: "New Timeline"
        shortcut: window.currentWorkspace === window.timelineWorkspaceIndex ? StandardKey.New : ""
        enabled: !window.componentWorkspaceActive && !timelineDocument.carouselBoardSession.documentId
        onTriggered: window.createTimelineFromMenu()
    }

    Action {
        id: newCanvasAction
        text: "New Canvas…"
        shortcut: window.currentWorkspace === window.libraryWorkspaceIndex ? StandardKey.New : ""
        enabled: !window.componentWorkspaceActive && !catalogAssets.busy && !catalogAssets.blankCanvasBusy && !editorPreview.photoPaintBusy &&
                 !editorPreview.brushStrokeActive && !window.switchingEditBoard &&
                 (typeof customerAccess === "undefined" || customerAccess.editingAllowed)
        onTriggered: {
            if (window.componentWorkspaceActive) {
                window.showPhotoRecipeNotice(qsTr("Apply or cancel component contents before creating another document."), false)
                return
            }
            window.createCarouselFromMenu()
        }
    }

    NewCanvasDialog {
        id: newCanvasDialog
        parent: Overlay.overlay
        busy: catalogAssets.blankCanvasBusy
        onCreateRequested: function(settings) {
            if (newCanvasDialog.resizing) {
                if (!window.commitActiveEditBoard() || !carouselDocument.resizeEditCanvas(settings)) {
                    errorText = carouselDocument.status
                    return
                }
                if (!window.activateEditBoard(0)) { errorText = carouselDocument.status; return }
                newCanvasDialog.close()
                return
            }
            if (!window.resolvePhotoNavigation()) {
                errorText = "Finish or cancel the current edit before creating another document."
                return
            }
            if (window.carouselPhotoEditActive && !window.commitActiveEditBoard()) {
                errorText = "Finish the current edit and save this document before creating another."
                return
            }
            if (!carouselDocument.createEditCanvas(settings)) {
                errorText = carouselDocument.status
                return
            }
            window.carouselPhotoEditActive = false
            if (!window.activateEditBoard(0)) {
                errorText = carouselDocument.status
                return
            }
            newCanvasDialog.close()
            window.switchToWorkspaceIndex(window.editWorkspaceIndex)
            window.pendingEditInspectorTab = "layers"
            window.activatePrimaryEditTool("layers")
        }
    }

    AppPopup {
        id: editBoardDialog
        objectName: "editBoardDialog"
        parent: Overlay.overlay
        property string operation: "rename"
        property string boardId: ""
        property string boardName: ""
        property string errorText: ""
        property Item focusReturnItem: null
        readonly property bool removing: operation === "remove"
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(420, parent ? parent.width - 32 : 420)))
        height: Math.min(popupAvailableHeight, Math.max(1, contentItem.implicitHeight + padding * 2))
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape
        onAboutToShow: focusReturnItem = window.activeFocusItem
        onOpened: {
            if (removing) editBoardDialogCancel.forceActiveFocus()
            else { editBoardNameField.forceActiveFocus(); editBoardNameField.selectAll() }
        }
        onClosed: Qt.callLater(function() {
            if (editBoardDialog.focusReturnItem && editBoardDialog.focusReturnItem.visible && editBoardDialog.focusReturnItem.enabled)
                editBoardDialog.focusReturnItem.forceActiveFocus(Qt.TabFocusReason)
            editBoardDialog.focusReturnItem = null
        })
        function apply() {
            const board = carouselDocument.slides[carouselDocument.currentIndex] || ({})
            if (!window.editBoardsActive || String(board.boardId || "") !== boardId) {
                errorText = "The active board changed. Close this dialog and try again."
                return
            }
            if (removing) {
                if (!window.modifyEditBoards("remove")) { errorText = carouselDocument.status; return }
            } else {
                if (!editBoardNameField.text.trim().length) return
                carouselDocument.setCurrentSlideName(editBoardNameField.text.trim())
                if (!carouselDocument.saveDocument()) { errorText = carouselDocument.saveError; return }
                window.carouselPhotoEditDisplayName = String(carouselDocument.slides[carouselDocument.currentIndex].boardName)
                window.localEditorDisplayName = window.carouselPhotoEditDisplayName
            }
            close()
        }
        background: PopupSurface {
            Accessible.role: Accessible.Dialog
            Accessible.name: editBoardDialog.removing ? "Remove board" : "Rename board"
        }
        contentItem: ColumnLayout {
            spacing: 16
            Label {
                text: editBoardDialog.removing ? "Remove board?" : "Rename board"
                color: theme.textStrong
                font.pixelSize: theme.typeDialogTitle
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: editBoardDialog.removing ? (carouselDocument.slideCount === 1
                        ? (String(carouselDocument.photoOrigin.assetId || "").length
                           ? "Remove the final board and return to its source photo. Undo restores this document."
                           : "Remove the final board and return to your saved collection. Undo restores this document.")
                        : Number((carouselDocument.slides[carouselDocument.currentIndex] || ({})).imageSpanCount || 1) > 1
                        ? "“" + editBoardDialog.boardName + "” will be removed. Its shared image and layers remain on the other boards."
                        : "“" + editBoardDialog.boardName + "” and its layers will be removed from this document.")
                                              : "Give this board a name you can recognize on the canvas."
                color: theme.textMuted
                wrapMode: Text.WordWrap
            }
            ToneTextField {
                id: editBoardNameField
                objectName: "editBoardNameField"
                Layout.fillWidth: true
                visible: !editBoardDialog.removing
                maximumLength: 120
                accessibleLabel: "Board name"
                onAccepted: editBoardDialog.apply()
            }
            Label {
                Layout.fillWidth: true
                visible: editBoardDialog.errorText.length > 0
                text: editBoardDialog.errorText
                color: theme.error
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                ActionButton {
                    id: editBoardDialogCancel
                    objectName: "editBoardDialogCancel"
                    text: "Cancel"
                    onClicked: editBoardDialog.close()
                }
                ActionButton {
                    objectName: "editBoardDialogApply"
                    text: editBoardDialog.removing ? "Remove board" : "Save name"
                    tone: editBoardDialog.removing ? "danger" : "primary"
                    enabled: editBoardDialog.removing || editBoardNameField.text.trim().length > 0
                    onClicked: editBoardDialog.apply()
                }
            }
        }
    }

    Action {
        id: openSelectedInEditAction
        objectName: "openSelectedInEditAction"
        text: "Open Selected Photo in Edit"
        shortcut: window.primaryShortcut("E")
        enabled: window.canOpenSelectedPhotoInEdit()
        onTriggered: window.openSelectedPhotoInEdit()
    }

    Action {
        id: openSelectedTimelineAction
        text: "Open Selected Timeline"
        enabled: window.canOpenSelectedTimeline()
        onTriggered: window.openSelectedTimelineDocument()
    }

    Action {
        id: saveTimelineAction
        text: "Save Timeline"
        shortcut: StandardKey.Save
        enabled: window.canSaveTimeline()
        onTriggered: window.saveCurrentTimeline()
    }

    Action {
        id: renderTimelineAction
        objectName: "renderTimelineAction"
        text: "Render Timeline…"
        enabled: {
            const view = window.activeTimelineView()
            return window.canRenderTimeline() && !timelineDocument.timelineSaveInProgress
                    && view !== null && !window.timelineHasActiveRenderJobs(view)
        }
        onTriggered: {
            const view = window.activeTimelineView()
            if (view && typeof view.openPublish === "function")
                view.openPublish(null)
        }
    }

    Action {
        id: saveBoardsAction
        text: "Save Boards"
        shortcut: StandardKey.Save
        enabled: (window.boardsWorkspaceActive || (window.photoWorkspaceActive && window.carouselPhotoEditActive)) &&
                 !carouselDocument.exportBusy && !editorPreview.photoPaintBusy && !editorPreview.brushStrokeActive
        onTriggered: {
            const workspace = editWorkspaceLoader.item
            if (window.carouselPhotoEditActive && workspace && workspace.photoTextOverlayHandle && workspace.photoTextOverlayHandle.editing)
                workspace.photoTextOverlayHandle.finish(true)
            if (!window.syncBoardPhotoEdit() || !carouselDocument.saveDocument())
                window.showPhotoRecipeNotice(carouselDocument.saveError || carouselDocument.status, false)
        }
    }

    Action {
        id: importFolderAction
        text: "Import Folder"
        shortcut: StandardKey.Open
        enabled: !catalogAssets.busy
        onTriggered: {
            window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
            catalogAssets.importFolder()
        }
    }

    Action {
        id: refreshLibraryAction
        text: "Refresh Library"
        shortcut: window.primaryShortcut("R")
        enabled: !catalogAssets.busy
        onTriggered: {
            window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
            catalogAssets.refresh()
        }
    }

    Action {
        id: queueExportAction
        text: "Start Export"
        shortcut: window.configuredShortcut("queue-export", window.primaryShiftShortcut("E"))
        enabled: window.canQueueExportFromMenu()
        onTriggered: window.queueExportFromMenu()
    }

    Action {
        id: preferencesAction
        text: "Preferences"
        shortcut: StandardKey.Preferences
        onTriggered: window.openPreferencesDialog(0)
    }

    Action {
        id: undoEditAction
        objectName: "mainPhotoUndoAction"
        text: "Undo"
        shortcut: window.primaryShortcut("Z")
        enabled: (window.currentWorkspace === window.libraryWorkspaceIndex && !window.libraryDocumentsActive && catalogAssets.canUndoCulling)
                 || (window.photoWorkspaceActive && window.canUndoPhotoWorkflow(false))
                 || (window.currentWorkspace === window.libraryWorkspaceIndex && carouselDocument.documentArchived && window.canUndoPhotoWorkflow(false))
                 || (window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.canUndo)
        onTriggered: {
            if (window.currentWorkspace === window.libraryWorkspaceIndex && carouselDocument.documentArchived && window.canUndoPhotoWorkflow(false))
                window.performPhotoUndo(false)
            else if (window.currentWorkspace === window.libraryWorkspaceIndex)
                catalogAssets.undoCulling()
            else if (window.currentWorkspace === window.timelineWorkspaceIndex)
                timelineDocument.undo()
            else
                window.performPhotoUndo(false)
        }
    }

    Action {
        id: redoEditAction
        objectName: "mainPhotoRedoAction"
        text: "Redo"
        shortcut: window.primaryShiftShortcut("Z")
        enabled: (window.currentWorkspace === window.libraryWorkspaceIndex && !window.libraryDocumentsActive && catalogAssets.canRedoCulling)
                 || (window.photoWorkspaceActive && window.canUndoPhotoWorkflow(true))
                 || (window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.canRedo)
        onTriggered: {
            if (window.currentWorkspace === window.libraryWorkspaceIndex)
                catalogAssets.redoCulling()
            else if (window.currentWorkspace === window.timelineWorkspaceIndex)
                timelineDocument.redo()
            else
                window.performPhotoUndo(true)
        }
    }

    Action {
        id: copyPhotoEditsAction
        text: "Copy Edits…"
        shortcut: window.photoWorkspaceActive ? window.primaryAltShortcut("C") : window.boardsWorkspaceActive ? "" : window.primaryShortcut("C")
        enabled: window.canCopyPhotoRecipe()
        onTriggered: window.openCopyPhotoEdits()
    }
    Action {
        id: shootMatchAction
        objectName: "shootMatchAction"
        text: qsTr("Match Selected Photos…")
        enabled: !window.componentWorkspaceActive && !(window.photoWorkspaceActive && window.editBoardsActive) &&
                 catalogAssets.selectedAssetCount > 1 && !catalogAssets.shootMatchBusy
        onTriggered: window.openShootMatchDialog()
    }
    Action {
        id: pastePhotoEditsAction
        text: "Paste Edits"
        shortcut: window.photoWorkspaceActive ? window.primaryAltShortcut("V") : window.boardsWorkspaceActive ? "" : window.primaryShortcut("V")
        enabled: window.canPastePhotoRecipe()
        onTriggered: window.pastePhotoEdits()
    }
    Shortcut {
        sequence: "Ctrl+C"
        enabled: window.macStyle && (copyLayersAction.enabled || copyPhotoEditsAction.enabled)
        onActivated: window.photoWorkspaceActive ? copyLayersAction.trigger() : window.openCopyPhotoEdits()
    }
    Shortcut {
        sequence: "Ctrl+V"
        enabled: window.macStyle && (pasteLayersAction.enabled || pastePhotoEditsAction.enabled)
        onActivated: window.photoWorkspaceActive ? pasteLayersAction.trigger() : window.pastePhotoEdits()
    }
    Action {
        id: copyLayersAction
        text: "Copy Layers"
        shortcut: window.primaryShortcut("C")
        enabled: window.photoWorkspaceActive && window.activeWorkspaceItem() && typeof window.activeWorkspaceItem().canCopyLayers === "function" &&
                 window.activeWorkspaceItem().canCopyLayers()
        onTriggered: window.activeWorkspaceItem().copySelectedLayers()
    }
    Action {
        id: pasteLayersAction
        objectName: "pasteLayersAction"
        text: "Paste Layers"
        shortcut: window.primaryShortcut("V")
        enabled: window.photoWorkspaceActive && window.activePhotoEditController().hasAsset &&
                 window.activePhotoEditController().hasCopiedPhotoLayers && !window.activePhotoEditController().photoLayerClipboardBusy &&
                 !carouselDocument.editBoardImageBusy &&
                 !window.activePhotoEditController().photoPaintBusy && !window.activePhotoEditController().brushStrokeActive &&
                 !window.focusItemSupports("paste") &&
                 (typeof customerAccess === "undefined" || customerAccess.editingAllowed)
        onTriggered: window.activeWorkspaceItem().pasteLayers(false)
    }
    Action {
        id: pasteLayersInPlaceAction
        objectName: "pasteLayersInPlaceAction"
        text: "Paste Layers in Place"
        shortcut: window.photoWorkspaceActive ? window.primaryShiftShortcut("V") : ""
        enabled: pasteLayersAction.enabled
        onTriggered: window.activeWorkspaceItem().pasteLayers(true)
    }
    Shortcut {
        // Portable Ctrl already binds Command through the Action on macOS.
        // Keep the physical Control alias distinct so the primary chord has one owner.
        sequence: "Meta+Shift+V"
        enabled: window.macStyle && pasteLayersInPlaceAction.enabled
        onActivated: pasteLayersInPlaceAction.trigger()
    }
    Action {
        id: undoPastedPhotoEditsAction
        text: "Undo Pasted Edits"
        enabled: !window.componentWorkspaceActive && window.photoRecipeWorkspace && catalogAssets.canUndoBatchRecipe
                 && catalogAssets.batchRecipeUndoLabel.indexOf("Pasted edits") === 0
        onTriggered: window.undoPastedPhotoEdits()
    }

    Action {
        id: selectAllAction
        text: "Select All"
        shortcut: StandardKey.SelectAll
        enabled: window.canSelectAllCurrentWorkspace()
        onTriggered: window.selectAllCurrentWorkspace()
    }

    Action {
        id: deselectAction
        text: "Deselect"
        shortcut: StandardKey.Deselect
        enabled: window.canDeselectCurrentWorkspace()
        onTriggered: window.deselectCurrentWorkspace()
    }

    Action {
        id: quitAction
        text: "Quit"
        shortcut: StandardKey.Quit
        onTriggered: Qt.quit()
    }

    Action {
        id: resetAdjustmentsAction
        text: window.editBoardsActive ? "Reset Board image" : "Revert photo to original"
        enabled: editorPreview.hasAsset && !window.componentWorkspaceActive
        onTriggered: window.resetCurrentPhotoEdits()
    }

    Action {
        id: toggleCompareAction
        text: "Compare View"
        checkable: true
        checked: editorPreview.compareMode
        enabled: editorPreview.hasAsset && !window.componentWorkspaceActive
        onTriggered: {
            const nextCompareMode = !editorPreview.compareMode
            editorPreview.compareMode = nextCompareMode
            window.activatePrimaryEditTool(nextCompareMode ? "compare" : "adjust")
        }
    }

    Action {
        id: toggleCropAction
        text: "Crop Overlay"
        checkable: true
        checked: editorPreview.cropEditing
        enabled: editorPreview.hasAsset && !window.componentWorkspaceActive
        onTriggered: editorPreview.cropEditing = !editorPreview.cropEditing
    }

    Action {
        id: adjustToolAction
        text: "Adjust Tool"
        shortcut: window.configuredShortcut("tool-adjust", window.primaryAltShortcut("A"))
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("adjust")
    }

    Action {
        id: cropToolAction
        text: "Crop Tool"
        shortcut: window.configuredShortcut("tool-crop", window.primaryAltShortcut("C"))
        enabled: editorPreview.hasAsset && !window.componentWorkspaceActive
        onTriggered: window.activatePrimaryEditTool("crop")
    }

    Action {
        id: masksToolAction
        text: "Masks Tool"
        shortcut: window.configuredShortcut("tool-masks", window.primaryAltShortcut("M"))
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("masks")
    }

    Action {
        id: brushToolAction
        text: "Brush Tool"
        shortcut: window.configuredShortcut("tool-brush", window.primaryAltShortcut("B"))
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("brush")
    }

    Action {
        id: compareToolAction
        text: "Compare Tool"
        shortcut: window.configuredShortcut("tool-compare", window.primaryAltShortcut("P"))
        enabled: editorPreview.hasAsset && !window.componentWorkspaceActive
        onTriggered: window.activatePrimaryEditTool("compare")
    }

    Action {
        id: exportToolAction
        text: "Export Tool"
        shortcut: window.configuredShortcut("tool-export", window.primaryAltShortcut("E"))
        enabled: !window.componentWorkspaceActive && (catalogAssets.selectedAssetId.length > 0 || editorPreview.hasAsset)
        onTriggered: window.activatePrimaryEditTool("export")
    }

    Action {
        id: addSequenceMarkerAction
        text: "Add Sequence Marker"
        shortcut: ""
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.clipCount > 0
        onTriggered: timelineDocument.addSequenceMarker()
    }

    Action {
        id: splitAtPlayheadAction
        text: "Split At Playhead"
        shortcut: window.configuredShortcut("timeline-split-alternate", window.primaryShortcut("\\"))
        enabled: window.canControlTimelinePlayback()
        onTriggered: {
            const timelineView = window.activeTimelineView()
            if (timelineView && timelineView.splitAtPlayhead)
                timelineView.splitAtPlayhead()
        }
    }

    Action {
        id: removeSelectedClipAction
        text: "Remove Selected Clip"
        shortcut: ""
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.currentIndex >= 0
        onTriggered: timelineDocument.removeSelectedClip()
    }

    Action {
        id: clearFinishedExportsAction
        text: "Clear Finished Jobs"
        enabled: jobQueue.historyMutationsEnabled && jobQueue.jobCount > 0
        onTriggered: jobQueue.clearFinishedJobs()
    }

    Action {
        id: openTimelineMediaBrowserAction
        text: "Open Media Browser"
        shortcut: window.primaryShiftShortcut("M")
        enabled: window.canOpenTimelineMediaBrowser()
        onTriggered: {
            const timelineView = window.activeTimelineView()
            if (timelineView && timelineView.openMediaPicker)
                timelineView.openMediaPicker()
        }
    }

    Action {
        id: toggleTimelinePlaybackAction
        text: timelineDocument.sequencePlaying ? "Pause Playback" : "Play Playback"
        shortcut: ""
        enabled: window.canControlTimelinePlayback()
        onTriggered: timelinePlayback.togglePlayback()
    }

    Action {
        id: stepTimelineBackwardAction
        text: "Step Back One Frame"
        shortcut: window.configuredShortcut("timeline-frame-back", "Left")
        enabled: window.canControlTimelinePlayback()
        onTriggered: timelinePlayback.stepFrames(-1)
    }

    Action {
        id: stepTimelineForwardAction
        text: "Step Forward One Frame"
        shortcut: window.configuredShortcut("timeline-frame-forward", "Right")
        enabled: window.canControlTimelinePlayback()
        onTriggered: timelinePlayback.stepFrames(1)
    }

    Action {
        id: addTimelineVideoTrackAction
        text: "Add Video Track"
        shortcut: window.configuredShortcut("timeline-add-video", "Ctrl+Shift+T")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.addVideoTrack()
    }

    Action {
        id: zoomInTimelineAction
        text: "Zoom In Timeline"
        shortcut: window.configuredShortcut("timeline-zoom-in", "Ctrl+=")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: {
            const tv = window.activeTimelineView()
            if (tv) tv.setTimelineZoom(tv.timelineZoom * 1.25, tv.timelineZoom * 0)
        }
    }

    Action {
        id: zoomOutTimelineAction
        text: "Zoom Out Timeline"
        shortcut: window.configuredShortcut("timeline-zoom-out", "Ctrl+-")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: {
            const tv = window.activeTimelineView()
            if (tv) tv.setTimelineZoom(tv.timelineZoom / 1.25, 0)
        }
    }

    Action {
        id: zoomToFitTimelineAction
        text: "Zoom To Fit Timeline"
        shortcut: window.configuredShortcut("timeline-zoom-fit", "Ctrl+0")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: {
            const tv = window.activeTimelineView()
            if (tv) tv.zoomToFit()
        }
    }

    Action {
        id: timelineSelectToolAction
        text: "Select Tool"
        shortcut: ""
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.activeTool = "select"
    }

    Action {
        id: timelineRazorToolAction
        text: "Razor Tool"
        shortcut: ""
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.activeTool = "razor"
    }

    Action {
        id: timelineSlipToolAction
        text: "Slip Tool"
        shortcut: ""
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.activeTool = "slip"
    }

    Action {
        id: timelineRollToolAction
        text: "Roll Tool"
        shortcut: ""
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.activeTool = "roll"
    }

    Action {
        id: timelineToggleSnapAction
        text: "Toggle Snapping"
        shortcut: ""
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: {
            const tv = window.activeTimelineView()
            if (tv) tv.snappingEnabled = !tv.snappingEnabled
        }
    }

    Action {
        id: timelineGoToStartAction
        text: "Go To Start"
        shortcut: window.configuredShortcut("timeline-start", "Home")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelinePlayback.seekSequenceSeconds(0)
    }

    Action {
        id: timelineGoToEndAction
        text: "Go To End"
        shortcut: window.configuredShortcut("timeline-end", "End")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelinePlayback.seekSequenceSeconds(timelineDocument.totalDurationSeconds)
    }

    Action {
        id: timelineDeleteClipAction
        text: "Delete Selected Clip"
        shortcut: ""
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.currentIndex >= 0
        onTriggered: timelineDocument.removeSelectedClip()
    }

    Action {
        id: timelineDropMarkerAction
        text: "Drop Marker"
        shortcut: ""
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.hasActiveTimeline
        onTriggered: timelineDocument.addSequenceMarker()
    }

    Action {
        id: timelineSelectAllAction
        text: "Select All Clips"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.clipCount > 0
        onTriggered: timelineDocument.selectAllClips()
    }

    Action {
        id: carouselAddSlideAction
        text: "Add Blank Board"
        shortcut: window.primaryShiftShortcut("A")
        enabled: window.boardsWorkspaceActive && carouselDocument.canAddSlide
        onTriggered: window.addEditBoard(false)
    }

    Action {
        id: carouselRemoveSlideAction
        text: "Remove Current Board"
        shortcut: ""
        enabled: window.boardsWorkspaceActive && carouselDocument.slideCount > 1
        onTriggered: window.requestEditBoardAction(carouselDocument.currentIndex, "remove")
    }

    Action {
        id: carouselMoveBackwardAction
        text: "Move Board Earlier"
        shortcut: ""
        enabled: window.boardsWorkspaceActive && carouselDocument.currentIndex > 0
        onTriggered: window.modifyEditBoards("earlier")
    }

    Action {
        id: carouselMoveForwardAction
        text: "Move Board Later"
        shortcut: ""
        enabled: window.boardsWorkspaceActive && carouselDocument.currentIndex < carouselDocument.slideCount - 1
        onTriggered: window.modifyEditBoards("later")
    }

    Action {
        id: carouselAssignSelectedMediaAction
        text: "Assign Selected Library Media"
        enabled: window.canAssignSelectedMediaToCarousel()
        onTriggered: window.assignSelectedMediaToCarousel()
    }

    Action {
        id: carouselExportAction
        text: "Export Boards"
        shortcut: window.primaryShiftShortcut("C")
        enabled: window.boardsWorkspaceActive && !carouselDocument.exportBusy
        onTriggered: carouselDocument.exportCarousel()
    }

    Action {
        id: carouselFormat4x5Action
        text: "4x5 Portrait"
        checkable: true
        checked: carouselDocument.documentFormat === "4x5"
        enabled: window.boardsWorkspaceActive
        onTriggered: carouselDocument.documentFormat = "4x5"
    }

    Action {
        id: carouselFormat5x4Action
        text: "5x4 Landscape"
        checkable: true
        checked: carouselDocument.documentFormat === "5x4"
        enabled: window.boardsWorkspaceActive
        onTriggered: carouselDocument.documentFormat = "5x4"
    }

    Action {
        id: switchToLibraryAction
        text: "Library"
        shortcut: window.configuredShortcut("workspace-library", window.primaryShortcut("1"))
        checkable: true
        checked: window.currentWorkspace === window.libraryWorkspaceIndex
        onTriggered: window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
    }

    Action {
        id: switchToEditAction
        text: "Edit"
        shortcut: window.configuredShortcut("workspace-edit", window.primaryShortcut("2"))
        checkable: true
        checked: window.currentWorkspace === window.editWorkspaceIndex
        onTriggered: window.switchToWorkspaceIndex(window.editWorkspaceIndex)
    }

    Action {
        id: switchToCarouselAction
        text: "Boards"
        shortcut: window.primaryShortcut("3")
        checkable: true
        checked: window.boardsWorkspaceActive
        onTriggered: window.switchToWorkspaceIndex(window.carouselWorkspaceIndex)
    }

    Action {
        id: switchToTimelineAction
        text: "Timeline"
        shortcut: window.primaryShortcut("4")
        checkable: true
        checked: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: window.switchToWorkspaceIndex(window.timelineWorkspaceIndex)
    }

    Action {
        id: switchToExportAction
        text: "Export"
        shortcut: window.primaryShortcut("5")
        checkable: true
        checked: window.currentWorkspace === window.exportWorkspaceIndex
        onTriggered: window.openContextualExport(null)
    }

    component TimelinePanelMenuItem: MenuItem {
        required property string panelId
        readonly property var timelineView: window.activeTimelineView()
        enabled: timelineView !== null
        checkable: true
        checked: timelineView !== null && timelineView.timelinePanelDockState
                 && timelineView.timelinePanelDockState.visible(panelId)
        onTriggered: {
            if (!timelineView || !timelineView.timelinePanelDockState)
                return
            if (timelineView.timelinePanelDockState.visible(panelId))
                timelineView.closeTimelinePanel(panelId)
            else
                timelineView.showTimelinePanel(panelId)
        }
    }

    Action {
        id: toggleSourcePanelAction
        text: "Media and Source"
        checkable: true
        checked: window.activeTimelineView() !== null && !window.activeTimelineView().sourcePanelCollapsed
                 && !window.activeTimelineView().sourcePanelHidden
        enabled: window.activeTimelineView() !== null
        onTriggered: {
            const timelineView = window.activeTimelineView()
            if (timelineView && timelineView.toggleSourcePanel)
                timelineView.toggleSourcePanel()
        }
    }

    Action {
        id: toggleEffectsPanelAction
        text: "Inspector"
        checkable: true
        checked: window.activeTimelineView() !== null && !window.activeTimelineView().effectsPanelCollapsed
        enabled: window.activeTimelineView() !== null
        onTriggered: {
            const timelineView = window.activeTimelineView()
            if (timelineView && timelineView.toggleEffectsPanel)
                timelineView.toggleEffectsPanel()
        }
    }

    Action {
        id: toggleSnappingAction
        text: "Snapping"
        checkable: true
        checked: window.activeTimelineView() !== null && window.activeTimelineView().snappingEnabled
        enabled: window.activeTimelineView() !== null
        onTriggered: {
            const timelineView = window.activeTimelineView()
            if (timelineView)
                timelineView.snappingEnabled = !timelineView.snappingEnabled
        }
    }

    Action {
        id: toggleRippleAction
        text: "Ripple Trim"
        checkable: true
        checked: window.activeTimelineView() !== null && window.activeTimelineView().rippleModeEnabled
        enabled: window.activeTimelineView() !== null
        onTriggered: {
            const timelineView = window.activeTimelineView()
            if (timelineView)
                timelineView.rippleModeEnabled = !timelineView.rippleModeEnabled
        }
    }

    Action {
        id: timelineEditingWorkspaceAction
        objectName: "timelineEditingWorkspaceAction"
        text: "Editing"
        checkable: true
        checked: window.activeTimelineView() !== null && window.activeTimelineView().timelineLayoutPreset === "edit"
        enabled: window.activeTimelineView() !== null
        onTriggered: window.activeTimelineView().applyTimelineLayoutPreset("edit")
    }

    Action {
        id: timelineColorWorkspaceAction
        objectName: "timelineColorWorkspaceAction"
        text: "Color"
        checkable: true
        checked: window.activeTimelineView() !== null && window.activeTimelineView().timelineLayoutPreset === "color"
        enabled: window.activeTimelineView() !== null
        onTriggered: window.activeTimelineView().applyTimelineLayoutPreset("color")
    }

    Action {
        id: timelineAudioWorkspaceAction
        objectName: "timelineAudioWorkspaceAction"
        text: "Audio"
        checkable: true
        checked: window.activeTimelineView() !== null && window.activeTimelineView().timelineLayoutPreset === "audio"
        enabled: window.activeTimelineView() !== null
        onTriggered: window.activeTimelineView().applyTimelineLayoutPreset("audio")
    }

    Action {
        id: timelineFinishWorkspaceAction
        objectName: "timelineFinishWorkspaceAction"
        text: "Finishing"
        checkable: true
        checked: window.activeTimelineView() !== null && window.activeTimelineView().timelineLayoutPreset === "finish"
        enabled: window.activeTimelineView() !== null
        onTriggered: window.activeTimelineView().applyTimelineLayoutPreset("finish")
    }

    Action {
        id: saveTimelineWorkspaceLayoutAction
        objectName: "saveTimelineWorkspaceLayoutAction"
        text: "Save Current Layout"
        enabled: window.activeTimelineView() !== null
        onTriggered: window.activeTimelineView().saveCurrentTimelineWorkspaceLayout()
    }

    Action {
        id: resetTimelineWorkspaceLayoutAction
        objectName: "resetTimelineWorkspaceLayoutAction"
        text: "Reset Current Layout"
        enabled: window.activeTimelineView() !== null
        onTriggered: window.activeTimelineView().resetCurrentTimelineWorkspaceLayout()
    }

    Action {
        id: timelineSequenceSettingsAction
        objectName: "timelineSequenceSettingsAction"
        text: "Sequence Settings…"
        enabled: window.activeTimelineView() !== null && timelineDocument.hasActiveTimeline
        onTriggered: window.activeTimelineView().openSequenceFormatControls()
    }

    Action {
        id: timelineProgramScopesAction
        objectName: "timelineProgramScopesAction"
        text: "Program Scopes"
        checkable: true
        checked: window.activeTimelineView() !== null && window.activeTimelineView().programScopesVisible
        enabled: window.activeTimelineView() !== null && timelineDocument.clipCount > 0
        onTriggered: {
            const view = window.activeTimelineView()
            if (view.programScopesVisible) view.closeProgramScopes()
            else view.openProgramScopes(false)
        }
    }

    Action {
        id: timelineAnimationPanelAction
        objectName: "timelineAnimationPanelAction"
        text: "Animation"
        enabled: window.activeTimelineView() !== null && timelineDocument.currentIndex >= 0
        onTriggered: {
            const view = window.activeTimelineView()
            view.openInspectorTab("animate")
            view.openCurveEditor(view.curveEditorParameter)
        }
    }

    Action {
        id: timelineColorReferenceToolsAction
        text: "Color Reference and LUT Tools…"
        enabled: window.activeTimelineView() !== null && timelineDocument.currentIndex >= 0
        onTriggered: window.activeTimelineView().openColorReferenceTools()
    }

    Action {
        id: timelineAudioMixerPanelAction
        text: "Audio Mixer"
        enabled: window.activeTimelineView() !== null && timelineDocument.clipCount > 0
        onTriggered: window.activeTimelineView().openAudioMixerPopup()
    }

    Action {
        id: timelinePanelOptionsAction
        text: "Timeline Options…"
        enabled: window.activeTimelineView() !== null
        onTriggered: window.activeTimelineView().openTimelinePanelOptions()
    }

    Action {
        id: restoreTimelineLayoutAction
        text: "Restore Timeline Layout"
        enabled: window.activeTimelineView() !== null
        onTriggered: appShell.resetTimelineWorkspaceSettings()
    }

    Action {
        id: minimizeWindowAction
        text: "Minimize"
        shortcut: StandardKey.Minimize
        onTriggered: window.showMinimized()
    }

    Action {
        id: bringWindowToFrontAction
        text: "Bring To Front"
        onTriggered: {
            window.showNormal()
            window.raise()
            window.requestActivate()
        }
    }

    Action {
        id: zoomWindowAction
        text: window.visibility === Window.Maximized ? "Restore Window" : "Zoom Window"
        onTriggered: {
            if (window.visibility === Window.Maximized)
                window.showNormal()
            else
                window.showMaximized()
        }
    }

    Action {
        id: keyboardShortcutsAction
        text: "Keyboard Shortcuts"
        onTriggered: window.openKeyboardShortcutsDialog()
    }

    Action {
        id: uiPrimitivesAction
        objectName: "uiPrimitivesAction"
        text: "UI Primitives"
        onTriggered: window.openUiPrimitivesPage()
    }

    Action {
        id: quickStartAction
        text: "Quick Start"
        onTriggered: {
            appShell.restartFirstRunExperience()
            window.openFirstRunGuide()
        }
    }

    Action {
        id: checkForUpdatesAction
        text: "Check for Updates"
        enabled: window.macStyle && macosUpdater && macosUpdater.available
        onTriggered: macosUpdater.checkForUpdates()
    }

    Action {
        id: commandPaletteAction
        text: "Command Palette"
        shortcut: window.configuredShortcut("command-palette", window.primaryShortcut("K"))
        onTriggered: {
            const carouselView = window.activeCarouselView()
            if (carouselView && typeof carouselView.openCarouselCommandPalette === "function")
                carouselView.openCarouselCommandPalette()
            else
                window.openCommandPaletteDialog()
        }
    }

    Action {
        id: aboutAction
        text: "About Looksy"
        onTriggered: window.openAboutDialog()
    }

    Action {
        id: runtimePathsAction
        text: "Show Runtime Paths"
        onTriggered: window.openPreferencesDialog(2)
    }

    Action {
        id: openAppDataFolderAction
        text: "Open App Data Folder"
        onTriggered: appShell.openAppDataFolder()
    }

    Action {
        id: openLogFolderAction
        text: "Open Logs Folder"
        onTriggered: appShell.openLogFolder()
    }

    Action {
        id: openBackupsFolderAction
        text: "Open Backups Folder"
        onTriggered: appShell.openBackupFolder()
    }

    Action {
        id: openCacheFolderAction
        text: "Open Cache Folder"
        onTriggered: appShell.openCacheFolder()
    }

    menuBar: MenuBar {
        objectName: "applicationMenuBar"
        Menu {
            objectName: "applicationFileMenu"
            title: "&File"
            MenuItem { action: newImportAction }
            MenuItem { action: newCanvasAction }
            MenuItem { action: newTimelineAction }
            MenuSeparator {}
            MenuItem { action: openSelectedInEditAction }
            MenuItem { action: openSelectedTimelineAction }
            MenuSeparator {}
            MenuItem { action: saveTimelineAction }
            MenuItem { action: renderTimelineAction }
            Menu {
                id: timelineProjectFileMenu
                objectName: "timelineProjectFileMenu"
                title: "Timeline Project"
                enabled: window.activeTimelineView() !== null
                Instantiator {
                    model: window.activeTimelineView() ? window.activeTimelineView().timelineHeaderMenuItems().concat(window.activeTimelineView().timelineSaveMenuItems()) : []
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.label
                        enabled: modelData.enabled !== false
                        onTriggered: {
                            const view = window.activeTimelineView()
                            if (view) view.handleTimelineHeaderMenuAction(modelData.key)
                        }
                    }
                    onObjectAdded: function(index, object) { timelineProjectFileMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { timelineProjectFileMenu.removeItem(object) }
                }
            }
            MenuItem { action: saveBoardsAction; visible: saveBoardsAction.enabled }
            MenuItem { action: importFolderAction }
            MenuItem { action: refreshLibraryAction }
            MenuSeparator {}
            MenuItem { action: queueExportAction }
            MenuSeparator {}
            MenuItem { action: preferencesAction }
            MenuItem { action: quitAction }
        }

        Menu {
            objectName: "applicationEditMenu"
            title: "&Edit"
            MenuItem { objectName: "applicationEditUndoItem"; action: undoEditAction }
            MenuItem { objectName: "applicationEditRedoItem"; action: redoEditAction }
            MenuItem { action: undoPastedPhotoEditsAction }
            MenuSeparator {}
            MenuItem { action: copyLayersAction }
            MenuItem { action: pasteLayersAction }
            MenuItem { action: pasteLayersInPlaceAction }
            MenuSeparator {}
            MenuItem { action: copyPhotoEditsAction }
            MenuItem { action: pastePhotoEditsAction }
            MenuItem { action: shootMatchAction }
            MenuItem { action: selectAllAction }
            MenuItem { action: deselectAction }
            MenuSeparator {}
            MenuItem { action: resetAdjustmentsAction }
            MenuItem { action: toggleCompareAction }
            MenuItem { action: toggleCropAction }
            MenuSeparator {}
            MenuItem { action: adjustToolAction }
            MenuItem { action: cropToolAction }
            MenuItem { action: masksToolAction }
            MenuItem { action: brushToolAction }
            MenuItem { action: compareToolAction }
            MenuItem { action: exportToolAction }
            MenuSeparator {}
            MenuItem { action: addSequenceMarkerAction }
            MenuItem { action: splitAtPlayheadAction }
            MenuItem { action: removeSelectedClipAction }
            MenuSeparator {}
            MenuItem { action: clearFinishedExportsAction }
        }

        Menu {
            title: "&View"
            MenuItem { action: switchToLibraryAction }
            MenuItem { action: switchToEditAction }
            MenuItem { action: switchToCarouselAction }
            MenuItem { action: switchToTimelineAction }
            MenuItem { action: switchToExportAction }
            MenuSeparator {}
            MenuItem {
                objectName: "canvasRulersMenuItem"
                readonly property var workspace: window.activeWorkspaceItem()
                text: "Show Rulers"
                visible: window.photoWorkspaceActive || window.currentWorkspace === window.timelineWorkspaceIndex
                enabled: visible && !!workspace && typeof workspace.canvasRulersVisible === "boolean"
                checkable: true
                checked: enabled && workspace.canvasRulersVisible === true
                onTriggered: {
                    if (enabled)
                        workspace.canvasRulersVisible = !workspace.canvasRulersVisible
                }
            }
            MenuSeparator { visible: window.photoWorkspaceActive || window.currentWorkspace === window.timelineWorkspaceIndex }
            MenuItem { action: commandPaletteAction }
            MenuSeparator { visible: window.activeTimelineView() !== null }
            Menu {
                id: timelineViewerMenu
                title: "Timeline Viewer"
                enabled: window.activeTimelineView() !== null
                Instantiator {
                    model: window.activeTimelineView() ? window.activeTimelineView().timelineOptionsInGroup("viewer") : []
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.label
                        enabled: modelData.enabled !== false
                        checkable: modelData.checked !== undefined
                        checked: modelData.checked === true
                        onTriggered: {
                            const view = window.activeTimelineView()
                            if (view) view.handleTimelineViewMenuAction(modelData.key)
                        }
                    }
                    onObjectAdded: function(index, object) { timelineViewerMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { timelineViewerMenu.removeItem(object) }
                }
            }
        }

        Menu {
            title: "&Window"
            MenuItem { action: minimizeWindowAction }
            MenuItem { action: zoomWindowAction }
            MenuItem { action: bringWindowToFrontAction }
            MenuSeparator {}
            Menu {
                title: "Workspaces"
                MenuItem { action: timelineEditingWorkspaceAction }
                MenuItem { action: timelineColorWorkspaceAction }
                MenuItem { action: timelineAudioWorkspaceAction }
                MenuItem { action: timelineFinishWorkspaceAction }
                MenuSeparator {}
                MenuItem { action: saveTimelineWorkspaceLayoutAction }
                MenuItem { action: resetTimelineWorkspaceLayoutAction }
            }
            Menu {
                title: "Panels"
                TimelinePanelMenuItem { panelId: "media"; text: "Media" }
                TimelinePanelMenuItem { panelId: "source"; text: "Source" }
                TimelinePanelMenuItem { panelId: "effects"; text: "Effects" }
                TimelinePanelMenuItem { panelId: "inspector"; text: "Inspector" }
                TimelinePanelMenuItem { panelId: "color"; text: "Color" }
                TimelinePanelMenuItem { panelId: "mixer"; text: "Audio Mixer" }
                TimelinePanelMenuItem { panelId: "keyframes"; text: "Keyframes" }
                TimelinePanelMenuItem { panelId: "titles"; text: "Titles" }
                TimelinePanelMenuItem { panelId: "captions"; text: "Captions" }
                TimelinePanelMenuItem { panelId: "transcript"; text: "Transcript" }
                TimelinePanelMenuItem { panelId: "scopes"; text: "Scopes" }
                MenuSeparator { }
                MenuItem { action: timelineColorReferenceToolsAction }
            }
            MenuItem { action: timelinePanelOptionsAction }
        }

        Menu {
            title: "&Timeline"
            enabled: window.currentWorkspace === window.timelineWorkspaceIndex
            MenuItem { action: newTimelineAction }
            MenuItem { action: saveTimelineAction }
            MenuItem { action: timelineSequenceSettingsAction }
            MenuSeparator {}
            MenuItem { action: openTimelineMediaBrowserAction }
            MenuItem { action: toggleTimelinePlaybackAction }
            MenuItem { action: stepTimelineBackwardAction }
            MenuItem { action: stepTimelineForwardAction }
            MenuSeparator {}
            MenuItem { action: toggleSourcePanelAction }
            MenuItem { action: toggleEffectsPanelAction }
            MenuItem { action: toggleSnappingAction }
            MenuItem { action: toggleRippleAction }
            MenuItem { action: restoreTimelineLayoutAction }
            MenuSeparator {}
            MenuItem { action: addSequenceMarkerAction }
            MenuItem { action: splitAtPlayheadAction }
            MenuItem { action: addTimelineVideoTrackAction }
            MenuItem { action: removeSelectedClipAction }
        }

        Menu {
            title: "&Boards"
            enabled: window.boardsWorkspaceActive
            MenuItem { action: newCanvasAction }
            MenuSeparator {}
            MenuItem { action: carouselAddSlideAction }
            MenuItem { action: carouselRemoveSlideAction }
            MenuItem { action: carouselMoveBackwardAction }
            MenuItem { action: carouselMoveForwardAction }
            MenuSeparator {}
            MenuItem { action: saveBoardsAction }
            MenuItem { action: carouselExportAction }
        }

        Menu {
            title: "&Help"
            MenuItem { action: quickStartAction }
            MenuItem { action: keyboardShortcutsAction }
            MenuItem { action: uiPrimitivesAction }
            MenuItem { action: checkForUpdatesAction }
            MenuSeparator {}
            MenuItem { action: aboutAction }
            MenuItem { action: runtimePathsAction }
            MenuSeparator {}
            MenuItem { action: openAppDataFolderAction }
            MenuItem { action: openLogFolderAction }
            MenuItem { action: openBackupsFolderAction }
            MenuItem { action: openCacheFolderAction }
        }
    }

    Component.onCompleted: {
        currentWorkspace = appShell.sessionRecoveryAvailable
                ? appShell.recoveredWorkspace()
                : appShell.preferredStartupWorkspace
        const startupSurface = activePerformanceSurface()
        const startupStartedAt = qmlNowMs()
        if (hasQmlPerformance())
            qmlPerformance.routeStarted(startupSurface, currentWorkspace, routeWorkflowId(startupSurface))
        workspaceInitialized = true
        timelinePlayback.workspaceActive = currentWorkspace === timelineWorkspaceIndex
        appShell.currentWorkspace = currentWorkspace
        if (currentWorkspace === libraryWorkspaceIndex) {
            libraryWorkspaceLoaded = true
        } else if (currentWorkspace === editWorkspaceIndex && !editBoardsActive) {
            editWorkspaceLoaded = true
        } else if (currentWorkspace === editWorkspaceIndex && editBoardsActive) {
            editWorkspaceLoaded = true
        } else if (currentWorkspace === timelineWorkspaceIndex) {
            timelineWorkspaceLoaded = true
        } else if (currentWorkspace === exportWorkspaceIndex) {
            exportWorkspaceLoaded = true
        }
        Qt.callLater(function() {
            if (!hasQmlPerformance())
                return
            qmlPerformance.routeReady(startupSurface,
                                      currentWorkspace,
                                      startupStartedAt,
                                      qmlNowMs(),
                                      workspaceLoaded(currentWorkspace),
                                      routeWorkflowId(startupSurface))
        })
        if (currentWorkspace !== editWorkspaceIndex)
            editWorkspacePrewarmTimer.restart()
        if (appShell.firstRunExperiencePending)
            firstRunGuideOpenTimer.restart()
        if (appShell.sessionRecoveryAvailable)
            recoveryRestoreTimer.restart()
    }

    onRecoverySurfaceVisibleChanged: {
        if (recoverySurfaceVisible && appShell.firstRunExperiencePending) {
            if (firstRunGuideLoader.item && firstRunGuideLoader.item.visible)
                firstRunGuideLoader.item.close()
            pendingFirstRunGuideOpen = false
            firstRunGuideOpenTimer.restart()
        }
    }

    function publishPhotoWorkIntent() {
        timelineDocument.setPhotoWorkIntent(currentWorkspace === editWorkspaceIndex,
                                           editorPreview.processing, editorPreview.assetId)
    }

    function publishForegroundWorkspaceIntent() {
        const index = workspaceTransitionActive && pendingWorkspaceIndex >= 0
                ? pendingWorkspaceIndex : currentWorkspace
        timelineDocument.setForegroundWorkspaceIntent(String(workspaceKinds[index] || "background"),
            true, workspaceTransitionActive,
            recentUserInput || (index === editWorkspaceIndex && editorPreview.processing))
    }

    onWorkspaceTransitionActiveChanged: {
        publishForegroundWorkspaceIntent()
        if (!workspaceTransitionActive) {
            if (currentWorkspace === editWorkspaceIndex && !pendingCanvasMediaPaths.length && !pendingCanvasVideos.length) canvasMediaTarget = null
            Qt.callLater(finishCanvasMediaSelection)
            Qt.callLater(startPendingBoardSequencePlayback)
        }
    }
    onRecentUserInputChanged: publishForegroundWorkspaceIntent()

    Connections {
        target: editorPreview
        function onRecipeSaveStateChanged() { recoveredRecipeSaveTimer.restart() }
        function onPhotoExportFinished(success, cancelled, path, error) {
            if (cancelled) window.showPhotoRecipeNotice(qsTr("Canvas export cancelled."), false)
            else if (success) window.showPhotoRecipeNotice(qsTr("Canvas exported: %1").arg(path), true)
            else window.showPhotoRecipeNotice(error || qsTr("The canvas export failed."), false)
        }
        function onProcessingChanged() { window.publishPhotoWorkIntent() }
        function onAssetChanged() { window.publishPhotoWorkIntent() }
        function onDocumentEditCommitted() { window.syncBoardPhotoEdit() }
        function onHistoryEntryRecorded(assetId, label) { window.recordPhotoWorkflowHistory("photo", assetId, label) }
        function onHistoryChanged() { window.finishPhotoWorkflowHistory() }
        function onHistoryRestoreFinished(assetId, success) {
            const pending = window.pendingPhotoWorkflowHistory
            if (!pending || String(assetId) !== pending.entry.resourceId) return
            if (success) window.finishPhotoWorkflowHistory()
            else {
                window.pendingPhotoWorkflowHistory = null
                window.replayingPhotoWorkflow = false
                window.showPhotoRecipeNotice(editorPreview.status || qsTr("History could not be restored. Your current edit is unchanged."), false)
            }
        }
    }

    Connections {
        target: timelineDocument
        function onRecoveryOpenCompleted(candidateId, succeeded) {
            if (String(candidateId || "") !== window.pendingTimelineRecoveryRouteId)
                return
            const workspace = window.pendingTimelineRecoveryWorkspace
            window.pendingTimelineRecoveryRouteId = ""
            window.pendingTimelineRecoveryWorkspace = null
            if (succeeded)
                window.switchToWorkspaceIndex(window.timelineWorkspaceIndex)
            else if (workspace && typeof workspace.showEditActionFeedback === "function")
                workspace.showEditActionFeedback(String(timelineDocument.status ||
                                                        "Recovery could not be opened. Your current project remains open."), true)
        }
    }

    Connections {
        target: carouselDocument
        function onHistoryEntryRecorded(documentId, label) { window.recordPhotoWorkflowHistory("board", documentId, label) }
        function onDocumentLoadFinished(requestId, documentId, success, applied) {
            if (Number(requestId) !== Number(window.pendingSavedBoardLoadRequestId) ||
                    String(documentId || "") !== window.pendingSavedBoardLoadDocumentId)
                return

            const wasRollback = window.pendingSavedBoardLoadIsRollback
            if (!success || !applied) {
                const failure = wasRollback
                        ? qsTr("The previous board could not be restored. It is still available in Saved Boards.")
                        : String(carouselDocument.status || qsTr("The saved board could not be opened."))
                window.finishSavedBoardOpenFailure(failure)
                return
            }

            if (wasRollback) {
                const failure = String(window.pendingSavedBoardLoadFailureDetail ||
                                       qsTr("The selected board could not be opened. Your previous board was restored."))
                const originalWorkspace = window.pendingSavedBoardOriginalWorkspace
                const restoreEditMode = window.pendingSavedBoardRestoreEditMode
                window.pendingSavedBoardLoadRequestId = 0
                window.pendingSavedBoardLoadDocumentId = ""
                window.pendingSavedBoardPreviousDocumentId = ""
                window.pendingSavedBoardLoadFailureDetail = ""
                window.pendingSavedBoardOriginalWorkspace = -1
                window.pendingSavedBoardRestoreEditMode = false
                window.pendingSavedBoardLoadIsRollback = false
                window.pendingSavedBoardLoadWasCancelled = false
                if (restoreEditMode && carouselDocument.hasActiveCarousel &&
                        !carouselDocument.documentArchived)
                    window.activateEditBoard(carouselDocument.currentIndex)
                if (window.workspaceTransitionActive) {
                    window.workspaceTransitionCommitDeferred = false
                    if (originalWorkspace === window.editWorkspaceIndex)
                        window.commitPendingWorkspaceTransition()
                    else
                        window.completeShellTransition()
                }
                window.showPhotoRecipeNotice(failure, false)
                return
            }

            if (carouselDocument.documentArchived) {
                window.pendingSavedBoardLoadRequestId = 0
                window.pendingSavedBoardLoadDocumentId = ""
                window.pendingSavedBoardPreviousDocumentId = ""
                window.pendingSavedBoardOriginalWorkspace = -1
                window.pendingSavedBoardRestoreEditMode = false
                window.pendingSavedBoardLoadIsRollback = false
                window.workspaceTransitionCommitDeferred = false
                window.completeShellTransition()
                window.returnToPhoto(true)
                return
            }

            if (!window.activateEditBoard(carouselDocument.currentIndex)) {
                window.pendingSavedBoardLoadFailureDetail = String(carouselDocument.status ||
                    qsTr("The selected board could not be opened."))
                const previousDocumentId = String(window.pendingSavedBoardPreviousDocumentId || "")
                const failedDocumentId = window.pendingSavedBoardLoadDocumentId
                window.carouselPhotoEditActive = false
                window.editBoardsActive = false
                window.carouselPhotoEditTargetKey = ""
                if (previousDocumentId.length > 0 && previousDocumentId !== failedDocumentId) {
                    window.pendingSavedBoardLoadIsRollback = true
                    window.pendingSavedBoardLoadDocumentId = previousDocumentId
                    const rollbackRequestId = Number(carouselDocument.loadDocumentAsync(previousDocumentId) || 0)
                    if (rollbackRequestId > 0) {
                        window.pendingSavedBoardLoadRequestId = rollbackRequestId
                        return
                    }
                }
                window.finishSavedBoardOpenFailure(window.pendingSavedBoardLoadFailureDetail)
                return
            }

            if (editWorkspaceLoader.item)
                editWorkspaceLoader.item.activateEditTool("layers")
            // Loading the document starts fresh native history. Preserve source
            // photo receipts whose per-asset histories remain cached.
            photoWorkflowUndoEntries = photoWorkflowUndoEntries.filter(function(entry) { return entry.owner !== "board" })
            photoWorkflowRedoEntries = photoWorkflowRedoEntries.filter(function(entry) { return entry.owner !== "board" })
            window.pendingSavedBoardLoadRequestId = 0
            window.pendingSavedBoardLoadDocumentId = ""
            window.pendingSavedBoardPreviousDocumentId = ""
            window.pendingSavedBoardLoadFailureDetail = ""
            window.pendingSavedBoardOriginalWorkspace = -1
            window.pendingSavedBoardRestoreEditMode = false
            window.pendingSavedBoardLoadIsRollback = false
            window.pendingSavedBoardLoadWasCancelled = false
            window.workspaceTransitionCommitDeferred = false
            window.commitPendingWorkspaceTransition()
        }
        function onEditBoardImageReplaced(success, error) {
            if (!window.editBoardsActive || !window.carouselPhotoEditActive) return
            const workspace = editWorkspaceLoader.item
            if (success) {
                const previousPath = window.localEditorFilePath
                window.carouselPhotoEditActive = false
                window.activateEditBoard(carouselDocument.currentIndex)
                window.loadLocalAssetIntoEditor(true)
                window.editBoardImageReplacement = {
                    documentId: carouselDocument.savedDocumentId, assetId: window.localEditorAssetId,
                    originalPath: previousPath, replacedPath: window.localEditorFilePath,
                    baseline: window.localEditorAdjustmentPatchJson
                }
                window.editBoardImageReplacementHistory = window.editBoardImageReplacementHistory.concat([window.editBoardImageReplacement])
            }
            if (workspace) workspace.showEditActionFeedback(error.length ? error : "Base image replaced · Layers preserved", error.length > 0)
        }
    }

    onEditBoardsActiveChanged: {
        publishPhotoWorkIntent()
        if (editBoardsActive) editWorkspaceLoaded = true
    }

    onCurrentWorkspaceChanged: {
        if (window.componentWorkspaceActive && currentWorkspace !== editWorkspaceIndex) {
            currentWorkspace = editWorkspaceIndex
            showPhotoRecipeNotice(qsTr("Apply or cancel component contents before changing workspaces."), false)
            return
        }
        if (currentWorkspace === carouselWorkspaceIndex) {
            if (carouselDocument.hasActiveCarousel) activateEditBoard(carouselDocument.currentIndex)
            currentWorkspace = editWorkspaceIndex
            return
        }
        publishForegroundWorkspaceIntent()
        publishPhotoWorkIntent()
        timelinePlayback.workspaceActive = currentWorkspace === timelineWorkspaceIndex
        if (workspaceInitialized)
            appShell.currentWorkspace = currentWorkspace

        if (currentWorkspace !== editWorkspaceIndex)
            syncEditorRecipeToCatalog()

        if (currentWorkspace === libraryWorkspaceIndex) {
            libraryWorkspaceLoaded = true
            Qt.callLater(function() {
                window.restorePrimaryStudioPanels(workspaceLoader.item)
            })
        } else if (currentWorkspace === editWorkspaceIndex && !editBoardsActive) {
            editWorkspaceLoaded = true
            if (editorUsesLocalAsset)
                loadLocalAssetIntoEditor()
            else
                loadSelectedAssetIntoEditor()
            Qt.callLater(function() {
                window.restorePrimaryStudioPanels(editWorkspaceLoader.item)
                if (editWorkspaceLoader.item && editWorkspaceLoader.item.contentWorkspace)
                    window.restorePrimaryStudioPanels(editWorkspaceLoader.item.contentWorkspace)
            })
        } else if (currentWorkspace === editWorkspaceIndex && editBoardsActive) {
            editWorkspaceLoaded = true
        } else if (currentWorkspace === timelineWorkspaceIndex) {
            timelineWorkspaceLoaded = true
        } else if (currentWorkspace === exportWorkspaceIndex) {
            exportWorkspaceLoaded = true
        }
    }

    onEditWorkspaceModeChanged: {
        if (editWorkspaceLoader.item) {
            editWorkspaceLoader.item.editTool = resolvedEditTool()
            window.restorePrimaryStudioPanels(editWorkspaceLoader.item, true)
            if (editWorkspaceLoader.item.contentWorkspace)
                window.restorePrimaryStudioPanels(editWorkspaceLoader.item.contentWorkspace, true)
        }
    }

    Timer {
        id: recoveryRestoreTimer
        interval: 250
        repeat: false
        onTriggered: {
            if (!window.restoreRecoveredSession())
                restart()
        }
    }

    Timer {
        id: recoveredRecipeSaveTimer
        interval: 250
        property var attempted: ({})
        onTriggered: {
            if (catalogAssets.loading) return
            const ids = editorPreview.recipeSaveRecoveryAssetIds
            for (let index = 0; index < ids.length; ++index) {
                const asset = catalogAssets.assetById(String(ids[index]))
                const path = String(asset.filePath || "")
                const key = String(ids[index]) + "|" + path
                if (path.length === 0 || asset.online === false || attempted[key]) continue
                if (editorPreview.retryRecoveredRecipeSave(String(ids[index]), path))
                    attempted[key] = true
            }
        }
    }

    Timer {
        id: editWorkspacePrewarmTimer
        interval: 700
        repeat: false
        onTriggered: {
            if (!window.workspaceTransitionActive && !window.anyAppActivity && !window.recentUserInput
                    && window.currentWorkspace !== window.sceneWorkspaceIndex)
                window.preloadEditWorkspace()
            else
                restart()
        }
    }

    Timer {
        id: firstRunGuideOpenTimer
        interval: 900
        repeat: false
        onTriggered: {
            if ((appShell.firstRunExperiencePending || window.pendingFirstRunGuideOpen)
                    && !catalogAssets.loading && !window.recoverySurfaceVisible)
                window.openFirstRunGuide()
            else if (appShell.firstRunExperiencePending || window.pendingFirstRunGuideOpen)
                restart()
        }
    }

    Timer {
        id: secondaryWorkspacePrewarmTimer
        interval: 1200
        repeat: false
        onTriggered: {
            if (!window.workspaceTransitionActive && !window.anyAppActivity && !window.recentUserInput
                    && window.currentWorkspace !== window.sceneWorkspaceIndex)
                window.preloadNextSecondaryWorkspace()
            else
                restart()
        }
    }

    Timer {
        id: timelineWorkspaceHoverPrewarmTimer
        interval: 450
        repeat: false
        onTriggered: {
            if (window.currentWorkspace === window.timelineWorkspaceIndex ||
                    window.timelineWorkspaceLoaded)
                return
            if (!window.workspaceTransitionActive && !window.anyAppActivity &&
                    window.currentWorkspace !== window.sceneWorkspaceIndex) {
                window.prewarmTimelineWorkspace()
                return
            }
            if (window.currentWorkspace !== window.sceneWorkspaceIndex)
                restart()
        }
    }

    Timer {
        id: qmlFrameSampler
        interval: 16
        repeat: true
        running: window.visible
        onTriggered: window.sampleQmlFrame(window.qmlNowMs())
    }

    ShootMatchDialog {
        id: shootMatchDialog
        parent: Overlay.overlay
        catalog: catalogAssets
        syncCurrentEdit: function() { return window.syncEditorRecipeToCatalog(); }
    }

    RecoveryReviewDialog {
        id: recoveredWorkReviewDialog
        objectName: "recoveredWorkReviewDialog"
        title: "Recovered work"
        summary: window.recoverySurfaceSummary
        details: window.recoveryDetailItems()
        recoveryEntries: editorPreview.recipeSaveRecoveryEntries
        archiveRecoveryHandler: function(assetId) { return editorPreview.archiveConflictingRecipeRecovery(assetId) }
        workspaceActionText: appShell.sessionRecoveryAvailable ? "Open recovered workspace"
                             : window.jobRecoveryNeeded || (jobQueue && jobQueue.hasRecoveryReport)
                               ? "Review jobs" : "Open library"
        onOpenWorkRequested: window.openRecoveredWorkspace()
    }

    CopyPhotoEditsDialog {
        id: copyPhotoEditsDialog
        parent: Overlay.overlay
        focusFallbackItem: window.workspaceFocusFallbackItem
        onCopyRequested: (patch, groups) => {
            if (editorPreview.copyPhotoRecipe(patch, groups)) {
                close()
                window.showPhotoRecipeNotice("Edits copied. Select photos and paste.", false)
            } else {
                errorText = "These edits could not be copied. Choose valid edit groups and try again."
            }
        }
    }
    AppPopup {
        id: photoRecipeNotice
        objectName: "photoRecipeNotice"
        parent: Overlay.overlay
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? parent.height - height - 32 : 0
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(520, parent ? parent.width - 32 : 520)))
        padding: theme.popupPadding
        closePolicy: Popup.NoAutoClose
        background: PopupSurface {}
        contentItem: RowLayout {
            Label { Layout.fillWidth: true; text: window.photoRecipeNoticeText; color: theme.textStrong; wrapMode: Text.WordWrap }
            ActionButton {
                objectName: "undoPastedPhotoEdits"
                text: "Undo"
                visible: window.photoRecipeNoticeUndo
                enabled: undoPastedPhotoEditsAction.enabled
                onClicked: window.undoPastedPhotoEdits()
            }
        }
    }
    Timer { id: photoRecipeNoticeTimer; interval: 7000; onTriggered: photoRecipeNotice.close() }

    Connections {
        target: catalogAssets

        function onBlankCanvasCreated(assetId) {
            newCanvasDialog.close()
            if (window.carouselPhotoEditActive && !window.finishCarouselPhotoEdit(true)) return
            window.editBoardsActive = false
            window.editorUsesLocalAsset = false
            window.loadSelectedAssetIntoEditor()
            window.pendingEditInspectorTab = "layers"
            window.activatePrimaryEditTool("shapes")
            const workspace = window.photoWorkspaceActive ? window.activeWorkspaceItem() : null
            if (workspace && workspace.objectName === "editStudioWorkspace") {
                workspace.editInspectorTab = "layers"
                window.pendingEditInspectorTab = ""
            }
        }

        function onBlankCanvasFailed(error) { newCanvasDialog.errorText = error }

        function onPhotoRecipeChanged(assetId, filePath, previousPatch, nextPatch, restoringHistory) {
            editorPreview.applyCatalogRecipeChange(assetId, filePath, previousPatch, nextPatch, restoringHistory)
        }

        function onRecipeReadinessChanged() {
            if (window.pendingCanvasRecipeIds.length && catalogAssets.selectedAssetsRecipesReady) {
                const sameSelection = JSON.stringify(window.pendingCanvasRecipeIds) === JSON.stringify(catalogAssets.selectedAssetIds)
                window.pendingCanvasRecipeIds = []
                if (sameSelection && window.canvasMediaTarget) Qt.callLater(window.addLibrarySelectionToCanvas)
            }
            if (editorPreview.hasAsset && !window.editorUsesLocalAsset &&
                    catalogAssets.assetRecipeReady(editorPreview.assetId) &&
                    (!window.photoWorkspaceActive ||
                     editorPreview.assetId !== catalogAssets.selectedAssetId))
                window.syncEditorRecipeToCatalog()

            if (window.pendingRecipeEditAssetId.length > 0) {
                if (catalogAssets.selectedAssetId !== window.pendingRecipeEditAssetId) {
                    window.pendingRecipeEditAssetId = ""
                } else if (catalogAssets.selectedRecipeReady) {
                    if (window.loadSelectedAssetIntoEditor()) {
                        window.pendingRecipeEditAssetId = ""
                        window.beginShellTransition(window.editWorkspaceIndex,
                                                    "Opening Edit",
                                                    window.workspaceSubtitles[window.editWorkspaceIndex],
                                                    window.workspaceAccent(window.editWorkspaceIndex),
                                                    "adjust")
                    }
                }
            }

            if (window.pendingMenuExportAssetId.length > 0) {
                if (catalogAssets.selectedAssetId !== window.pendingMenuExportAssetId) {
                    window.pendingMenuExportAssetId = ""
                } else if (catalogAssets.selectedRecipeReady) {
                    window.pendingMenuExportAssetId = ""
                    window.queueExportFromMenu()
                }
            }

            if (appShell.sessionRecoveryAvailable && !window.sessionRestoreApplied &&
                    catalogAssets.recipeLoadErrorCount === 0)
                window.restoreRecoveredSession()
        }

        // Catalog selection leaves the open editor document intact. Activation
        // loads it explicitly, including clicks from the editor's filmstrip.

        function onLoadingChanged() {
            if (!catalogAssets.loading) recoveredRecipeSaveTimer.restart()
            if (appShell.sessionRecoveryAvailable && !window.sessionRestoreApplied)
                recoveryRestoreTimer.restart()
        }
    }

    Connections {
        target: timelineDocument
        function onTimelineStateChanged() {
            if (!timelineDocument.hasActiveTimeline && !timelineDocument.carouselBoardSession.documentId) window.retainBoardTimelineView = false
        }
        function onCarouselBoardSequenceClosed(applied) {
            window.pendingBoardSequencePlayback = false
            timelinePlayback.pause()
            if (applied && window.carouselPhotoEditActive) {
                window.switchingEditBoard = true
                const payload = carouselDocument.editBoardPayload(carouselDocument.currentIndex)
                window.localEditorAdjustmentPatchJson = String(payload.adjustmentPatchJson || "")
                window.loadLocalAssetIntoEditor(true)
                window.switchingEditBoard = false
            }
            window.switchToWorkspaceIndex(window.editWorkspaceIndex)
            const workspace = editWorkspaceLoader.item
            if (workspace) {
                workspace.editSequenceBackgroundMode = true
                workspace.boardSequencePlaying = false
                workspace.editInspectorTab = "layers"
            }
        }
    }

    Connections {
        target: timelinePlayback
        function onStateChanged() {
            if (window.pendingBoardSequencePlayback) Qt.callLater(window.startPendingBoardSequencePlayback)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: theme.ink950
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            objectName: "customerReadOnlyNotice"
            Layout.fillWidth: true
            Layout.margins: visible ? 8 : 0
            visible: typeof customerAccess !== "undefined" && !customerAccess.editingAllowed
            Label {
                Layout.fillWidth: true
                text: "Editing needs an active trial or purchase. You can still view, save, and export existing work."
                color: theme.textStrong
                wrapMode: Text.WordWrap
            }
            ActionButton {
                text: "Account"
                compact: true
                onClicked: window.openPreferencesDialog(5)
            }
        }

        Rectangle {
            objectName: "appNavigationBar"
            Layout.fillWidth: true
            Layout.preferredHeight: theme.navHeight
            visible: !window.timelineProgramFullScreenActive && !window.sceneImmersiveActive
            color: theme.ink950

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.alpha(theme.lineSoft, 0.78)
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: window.width < 1100 ? 10 : 18

                Canvas {
                    objectName: "appNavigationBrandMark"
                    Layout.preferredWidth: theme.scaled(24)
                    Layout.preferredHeight: theme.scaled(24)
                    Layout.alignment: Qt.AlignVCenter
                    property url markSource: "qrc:/photo-editor/brand/looksy-app-mark.png"
                    Component.onCompleted: loadImage(markSource)
                    onImageLoaded: requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        if (!isImageLoaded(markSource)) return
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = theme.ink950
                        ctx.fillRect(0, 0, width, height)
                        ctx.globalCompositeOperation = "lighter"
                        ctx.drawImage(markSource, 300, 320, 460, 460, 0, 0, width, height)
                    }
                    Accessible.role: Accessible.Graphic
                    Accessible.name: "Looksy"
                }

                RowLayout {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: true
                    spacing: window.width < 1100 ? 8 : 18

                    Item { Layout.fillWidth: true }

                    Repeater {
                        model: workspaceTitles

                        delegate: NavigationTab {
                            id: workspaceTab

                            required property int index
                            required property string modelData

                            Component.onCompleted: {
                                if (workspaceTab.index === window.currentWorkspace)
                                    window.workspaceFocusFallbackItem = workspaceTab
                            }
                            Component.onDestruction: {
                                if (window.workspaceFocusFallbackItem === workspaceTab)
                                    window.workspaceFocusFallbackItem = null
                            }
                            Connections {
                                target: window
                                function onCurrentWorkspaceChanged() {
                                    if (workspaceTab.index === window.currentWorkspace)
                                        window.workspaceFocusFallbackItem = workspaceTab
                                }
                            }
                            visible: workspaceTab.index !== window.carouselWorkspaceIndex
                            objectName: workspaceTab.index === window.carouselWorkspaceIndex ? "workspaceTabCarousel" : "workspaceTab" + workspaceTab.modelData
                            text: workspaceTab.modelData
                            Layout.preferredWidth: visible ? Math.max(theme.navItemMinWidth, tabLabel.implicitWidth + 18) : 0
                            Layout.preferredHeight: theme.navHeight
                            flat: true
                            hoverEnabled: true
                            focusPolicy: Qt.TabFocus
                            activeFocusOnTab: true
                            AppToolTip {
                                id: sharedTooltip1
                                targetItem: workspaceTab
                                visible: workspaceTab.visible && (workspaceTab.hovered)
                                delay: 350
                                timeout: 2800
                                text: workspaceSubtitles[workspaceTab.index]
                            }
                            onHoveredChanged: {
                                if (workspaceTab.index !== window.timelineWorkspaceIndex)
                                    return
                                if (hovered)
                                    timelineWorkspaceHoverPrewarmTimer.restart()
                                else if (!window.timelineWorkspaceLoaded)
                                    timelineWorkspaceHoverPrewarmTimer.stop()
                            }
                            onActiveFocusChanged: {
                                if (workspaceTab.index !== window.timelineWorkspaceIndex)
                                    return
                                if (activeFocus)
                                    timelineWorkspaceHoverPrewarmTimer.restart()
                                else if (!window.timelineWorkspaceLoaded)
                                    timelineWorkspaceHoverPrewarmTimer.stop()
                            }
                            onClicked: {
                                if (workspaceTab.index === window.timelineWorkspaceIndex)
                                    timelineWorkspaceHoverPrewarmTimer.stop()
                                if (workspaceTab.index === window.exportWorkspaceIndex) {
                                    window.openContextualExport(workspaceTab)
                                    return
                                }
                                window.switchToWorkspaceIndex(index)
                            }

                            selected: workspaceTab.index === window.currentWorkspace
                            surfaceVerticalInset: theme.space8
                            contentItem: Item {
                                Text {
                                    id: tabLabel
                                    objectName: "workspaceTabLabel" + workspaceTab.modelData
                                    anchors.centerIn: parent
                                    text: workspaceTab.modelData
                                    color: workspaceTab.index === window.currentWorkspace
                                           ? theme.textStrong
                                           : (workspaceTab.hovered ? theme.textMain : theme.textMuted)
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeControl
                                    font.weight: theme.weightControl
                                    font.letterSpacing: 0
                                }

                                Rectangle {
                                    visible: window.workspaceBadgeText(workspaceTab.index).length > 0
                                    anchors.left: tabLabel.right
                                    anchors.leftMargin: 5
                                    anchors.verticalCenter: tabLabel.verticalCenter
                                    width: 6
                                    height: 6
                                    radius: 0
                                    color: window.workspaceBadgeEmphasis(workspaceTab.index)
                                           ? theme.signalRed
                                           : window.workspaceAccent(workspaceTab.index)
                                }
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    objectName: "recoveryHeaderStatusIndicator"
                    visible: window.compactRecoveryStatus
                    Layout.alignment: Qt.AlignVCenter
                    spacing: theme.gapCommand
                    Accessible.role: Accessible.StatusBar
                    Accessible.name: "Work recovered"
                    Accessible.description: appShell.sessionRecoverySummary

                    ActionButton {
                        objectName: "recoveryHeaderReviewButton"
                        text: "Recovered"
                        iconName: "check"
                        compact: true
                        tooltipText: appShell.sessionRecoverySummary + " · Review recovered work"
                        onClicked: window.reviewRecoveredWork()
                    }
                    ActionButton {
                        objectName: "recoveryHeaderDismissButton"
                        iconName: "close"
                        showText: false
                        compact: true
                        accessibleName: "Dismiss recovered work status"
                        tooltipText: "Dismiss recovered work status"
                        onClicked: window.dismissRecoverySurface()
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: window.width < 1100 ? 160 : 250
                    spacing: 3
                    visible: !window.compactRecoveryStatus
                        && ((window.currentSelectionLabel().length > 0
                             && window.currentWorkspace !== window.timelineWorkspaceIndex)
                            || window.appActivityStatusVisible)

                    Text {
                        id: selectionStatusText
                        objectName: "topSelectionStatusLabel"
                        Layout.fillWidth: true
                        visible: window.currentSelectionLabel().length > 0
                                 && window.currentWorkspace !== window.exportWorkspaceIndex
                                 && !(window.currentWorkspace === window.libraryWorkspaceIndex && window.libraryWorkspaceMode === "import")
                        text: window.currentSelectionLabel()
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.letterSpacing: 0
                        horizontalAlignment: Text.AlignRight
                        fontSizeMode: Text.FixedSize
                        elide: Text.ElideRight
                        Accessible.name: text
                        HoverHandler { id: selectionStatusHover }
                        AppToolTip {
                            targetItem: selectionStatusText
                            visible: selectionStatusHover.hovered
                            text: selectionStatusText.text
                        }
                    }

                    Text {
                        id: workspaceStatusText
                        objectName: "topWorkspaceStatusLabel"
                        Layout.fillWidth: true
                        visible: window.appActivityStatusVisible && !window.libraryLoadingSceneVisible
                                 && !(window.photoWorkspaceActive && window.previewUpdateVisible) && !window.workspaceOwnsTransferActivity
                        text: window.appActivityStatusTitle
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.letterSpacing: 0
                        horizontalAlignment: Text.AlignRight
                        fontSizeMode: Text.FixedSize
                        elide: Text.ElideRight
                        Accessible.name: text
                        HoverHandler { id: workspaceStatusHover }
                        AppToolTip {
                            targetItem: workspaceStatusText
                            visible: workspaceStatusHover.hovered
                            text: workspaceStatusText.text
                        }
                    }
                }
            }

            ProgressStrip {
                objectName: "additiveActivityProgressLine"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 4
                visible: window.appActivityStatusVisible && !window.libraryLoadingSceneVisible
                         && !(window.photoWorkspaceActive && window.previewUpdateVisible) && !window.workspaceOwnsTransferActivity
                         && !window.appActivityErrorVisible
                         && !window.recoveryBannerVisible
                         && !window.timelineProgramFullScreenActive && !window.sceneImmersiveActive
                progress: window.primaryActivityProgress
                accent: window.primaryActivityAccent
                z: 2
            }

            ActionButton {
                objectName: "cancelActivityInNavigation"
                anchors.right: parent.right
                anchors.rightMargin: 278
                anchors.verticalCenter: parent.verticalCenter
                visible: window.appActivityStatusVisible && !window.libraryLoadingSceneVisible
                         && !window.appActivityErrorVisible
                         && !window.recoveryBannerVisible && window.appActivityStatusCancelable
                         && !window.workspaceOwnsTransferActivity
                width: 22
                height: 22
                compact: true
                embedded: true
                tone: "tertiary"
                iconName: "close"
                showText: false
                tooltipText: "Cancel current task"
                Accessible.name: "Cancel current task"
                onClicked: window.cancelPrimaryActivity()
                z: 3
            }
        }

        ActivityStatusBar {
            id: appActivityStatusBar
            objectName: window.recoveryBannerVisible ? "recoveryStatusIndicator"
                        : window.appActivityErrorVisible ? "failureActivityIndicator" : "floatingActivityIndicator"
            visible: (window.appActivityErrorVisible || window.recoveryBannerVisible)
                     && !window.compactRecoveryStatus
                     && (!window.workspaceOwnsTransferActivity || window.recoveryBannerVisible)
                     && !window.timelineProgramFullScreenActive
                     && !window.sceneImmersiveActive
            title: window.recoveryBannerVisible ? "Work recovered" : window.appActivityStatusTitle
            detail: window.recoveryBannerVisible ? appShell.sessionRecoverySummary : window.appActivityStatusDetail
            progressValue: window.recoveryBannerVisible ? 1
                           : window.appActivityErrorVisible ? -1 : window.primaryActivityProgress
            failed: window.appActivityErrorVisible
            accent: window.recoveryBannerVisible ? theme.success : window.primaryActivityAccent
            statusIcon: window.recoveryBannerVisible ? "check" : ""
            dismissible: window.recoveryBannerVisible
            actionText: window.recoveryBannerVisible ? "Review work" : ""
            cancelable: !window.recoveryBannerVisible && window.appActivityStatusCancelable
            retryable: window.appActivityStatusRetryable
            onCancelRequested: if (!window.recoveryBannerVisible) window.cancelPrimaryActivity()
            onRetryRequested: window.handleAppActivityStatusAction()
            onActionRequested: if (window.recoveryBannerVisible) window.reviewRecoveredWork()
            onDismissRequested: if (window.recoveryBannerVisible) window.dismissRecoverySurface()
        }

        RecoveryStatusPanel {
            id: recoverySurface
            objectName: "recoverySummarySurface"
            Layout.fillWidth: true
            visible: window.recoverySurfaceVisible
                     && !window.timelineProgramFullScreenActive && !window.sceneImmersiveActive
            catalogFailure: window.catalogRecoveryNeeded
            jobFailure: window.jobRecoveryNeeded
            repairPending: appShell.catalogRepairConfirmationPending
            summary: window.recoverySurfaceSummary
            details: window.recoveryDetailItems()
            onRetryRequested: catalogAssets.refresh()
            onBackupRequested: appShell.createCatalogBackup()
            onRepairRequested: appShell.requestCatalogRepair()
            onConfirmRepairRequested: {
                if (appShell.confirmCatalogRepair())
                    catalogAssets.refresh()
            }
            onCancelRepairRequested: appShell.cancelCatalogRepair()
            onReviewRequested: window.reviewRecoveredWork()
            onDismissRequested: window.dismissRecoverySurface()
        }

        Rectangle {
            objectName: "mainWorkspaceSurface"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 0
            radius: 0
            color: theme.alpha(theme.glassBase, 0.78)
            border.width: 0
            border.color: "transparent"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: theme.alpha(theme.polar, 0.045)
            }

            Loader {
                id: workspaceLoader
                anchors.fill: parent
                anchors.margins: 0
                active: window.libraryWorkspaceLoaded || window.currentWorkspace === window.libraryWorkspaceIndex
                visible: window.currentWorkspace === window.libraryWorkspaceIndex
                sourceComponent: active ? libraryView : null
                onLoaded: {
                    window.noteWorkspaceComponentCreated(window.libraryWorkspaceIndex)
                    window.markWorkspaceLoaded(window.libraryWorkspaceIndex)
                    window.maybeFinishWorkspaceTransition(window.libraryWorkspaceIndex)
                }
            }

            Loader {
                id: editWorkspaceLoader
                anchors.fill: parent
                anchors.margins: 0
                anchors.topMargin: window.componentWorkspaceActive
                                  ? componentWorkspaceWorkspaceBar.height + theme.space8 : 0
                active: window.editWorkspaceLoaded || window.photoWorkspaceActive
                visible: window.photoWorkspaceActive
                enabled: !(window.componentWorkspaceActive && window.componentWorkspaceController.busy === true)
                sourceComponent: active ? editView : null
                onLoaded: {
                    window.noteWorkspaceComponentCreated(window.editWorkspaceIndex)
                    window.markWorkspaceLoaded(window.editWorkspaceIndex)
                    if (item) {
                        item.editTool = window.resolvedEditTool()
                        window.restorePrimaryStudioPanels(item)
                        if (item.contentWorkspace)
                            window.restorePrimaryStudioPanels(item.contentWorkspace)
                    }
                    if (item && window.pendingEditInspectorTab.length > 0) {
                        item.editInspectorTab = window.pendingEditInspectorTab
                        window.pendingEditInspectorTab = ""
                    }
                    window.applyPendingEditToolActivation()
                    window.maybeFinishWorkspaceTransition(window.editWorkspaceIndex)
                }
            }

            PhotoComponentWorkspaceBar {
                id: componentWorkspaceWorkspaceBar
                objectName: "photoComponentWorkspaceBar"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: theme.space12
                anchors.rightMargin: theme.space12
                z: 300
                orchestrator: window.componentWorkspaceController
                workspace: editWorkspaceLoader.item
            }

            Connections {
                target: editWorkspaceLoader.item
                enabled: editWorkspaceLoader.item !== null
                ignoreUnknownSignals: true

                function onPageContentReadyChanged() {
                    if (editWorkspaceLoader.item && editWorkspaceLoader.item.pageContentReady) {
                        window.applyPendingEditToolActivation()
                        window.maybeFinishWorkspaceTransition(window.editWorkspaceIndex)
                    }
                }

                function onAssetActivated(destination) {
                    if (window.componentWorkspaceActive &&
                            ["library", "export", "import", "edit", "timeline"].indexOf(String(destination)) >= 0) {
                        window.showPhotoRecipeNotice(qsTr("Apply or cancel component contents before changing documents."), false)
                        return
                    }
                    if (destination === "library") {
                        window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
                        return
                    }
                    if (destination === "masks") {
                        window.activatePrimaryEditTool("masks")
                        return
                    }
                    if (destination === "compare") {
                        window.activatePrimaryEditTool("compare")
                        return
                    }
                    if (destination === "export") {
                        window.switchToWorkspaceIndex(window.exportWorkspaceIndex)
                        return
                    }
                }
            }

            Binding {
                target: editWorkspaceLoader.item
                property: "workspaceActive"
                value: window.photoWorkspaceActive
                when: editWorkspaceLoader.item !== null
                restoreMode: Binding.RestoreBindingOrValue
            }

            Loader {
                id: timelineWorkspaceLoader
                objectName: "timelineWorkspaceLoader"
                anchors.fill: parent
                anchors.margins: 0
                asynchronous: timelineWorkspaceLoader.status === Loader.Loading
                              || window.currentWorkspace !== window.timelineWorkspaceIndex
                active: window.timelineWorkspaceLoaded || window.currentWorkspace === window.timelineWorkspaceIndex
                visible: window.currentWorkspace === window.timelineWorkspaceIndex
                // Returning from a board briefly closes its child document.
                // Keep the editor instance so reopening does not race an
                // asynchronous Setup-page replacement and decoder teardown.
                source: Qt.resolvedUrl(timelineDocument.hasActiveTimeline || window.retainBoardTimelineView
                                       ? "workspaces/TimelineView.qml"
                                       : "workspaces/TimelineSetupPage.qml")
                onLoaded: {
                    window.noteWorkspaceComponentCreated(window.timelineWorkspaceIndex)
                    window.markWorkspaceLoaded(window.timelineWorkspaceIndex)
                    window.maybeFinishWorkspaceTransition(window.timelineWorkspaceIndex)
                    Qt.callLater(window.startPendingBoardSequencePlayback)
                }
            }

            Connections {
                target: timelineWorkspaceLoader.item
                enabled: timelineWorkspaceLoader.item !== null
                ignoreUnknownSignals: true
                function onPageContentReadyChanged() {
                    if (timelineWorkspaceLoader.item
                            && timelineWorkspaceLoader.item.pageContentReady === true)
                        window.maybeFinishWorkspaceTransition(window.timelineWorkspaceIndex)
                }
            }

            Rectangle {
                id: timelineWorkspaceLoadingSurface
                objectName: "timelineWorkspaceLoadingSurface"
                anchors.fill: parent
                anchors.margins: 0
                z: 1000
                visible: window.currentWorkspace === window.timelineWorkspaceIndex
                         && timelineWorkspaceLoader.status === Loader.Loading
                color: theme.alpha(theme.ink950, 0.94)
                Accessible.role: Accessible.ProgressBar
                Accessible.name: "Preparing Timeline workspace"
                Accessible.description: "Loading track controls, the Program Monitor, and editing tools."

                LooksyLoadingScene {
                    anchors.centerIn: parent
                    width: Math.max(100, Math.min(560, parent.width - 40, (parent.height - 100) * 1.5))
                    scene: "timeline"
                    title: "Preparing Timeline"
                    detail: "Opening tracks, preview, and editing tools…"
                    active: timelineWorkspaceLoadingSurface.visible
                }
            }

            Loader {
                id: exportWorkspaceLoader
                anchors.fill: parent
                anchors.margins: 0
                active: window.exportWorkspaceLoaded || window.currentWorkspace === window.exportWorkspaceIndex
                visible: window.currentWorkspace === window.exportWorkspaceIndex
                source: Qt.resolvedUrl("workspaces/ExportWorkspaceHost.qml")
                onLoaded: {
                    window.noteWorkspaceComponentCreated(window.exportWorkspaceIndex)
                    window.markWorkspaceLoaded(window.exportWorkspaceIndex)
                    window.maybeFinishWorkspaceTransition(window.exportWorkspaceIndex)
                }
            }

            Loader {
                id: sceneWorkspaceLoader
                anchors.fill: parent
                anchors.margins: 0
                active: window.sceneWorkspaceLoaded || window.currentWorkspace === window.sceneWorkspaceIndex
                visible: window.currentWorkspace === window.sceneWorkspaceIndex
                source: Qt.resolvedUrl("workspaces/SceneWorkspace.qml")
                onLoaded: {
                    item.controller = gaussianScene
                    item.catalog = catalogAssets
                    window.deliverPendingSceneLibraryEntry()
                    window.noteWorkspaceComponentCreated(window.sceneWorkspaceIndex)
                    // Do not mutate Loader.active's dependency inside its load.
                    Qt.callLater(function() {
                        window.markWorkspaceLoaded(window.sceneWorkspaceIndex)
                        window.maybeFinishWorkspaceTransition(window.sceneWorkspaceIndex)
                    })
                }
            }

            Connections {
                target: sceneWorkspaceLoader.item
                enabled: sceneWorkspaceLoader.item !== null
                function onLibraryScenesRequested() {
                    window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
                    Qt.callLater(function() { if (workspaceLoader.item) workspaceLoader.item.libraryCollection = "scenes" })
                }
                function onExitViewerRequested() {
                    window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
                }
                function onPhotoReadyForEditing(assetId, openEditor) {
                    // Import may finish after navigation or a different scene was opened.
                    // Keep the imported photo, but do not steal the current workspace.
                    if (!openEditor || window.currentWorkspace !== window.sceneWorkspaceIndex)
                        return
                    window.syncEditorRecipeToCatalog(assetId)
                    catalogAssets.clearFilters()
                    if (!catalogAssets.selectAssetById(assetId, true))
                        return
                    if (window.carouselPhotoEditActive && !window.finishCarouselPhotoEdit(true)) return
                    window.editBoardsActive = false
                    window.editorUsesLocalAsset = false
                    window.carouselPhotoEditActive = false
                    window.switchToWorkspaceIndex(window.editWorkspaceIndex)
                }
            }

            Connections {
                target: exportWorkspaceLoader.item
                enabled: exportWorkspaceLoader.item !== null
                ignoreUnknownSignals: true

                function onPresetManagementRequested(action) {
                    window.openEditPresetPanelFromExport(action)
                }

                function onAssetActivated(destination) {
                    if (destination === "library") {
                        window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
                        return
                    }
                    if (destination === "import") {
                        window.beginImportWorkspace()
                        return
                    }
                    if (destination === "edit") {
                        window.switchToWorkspaceIndex(window.editWorkspaceIndex)
                        return
                    }
                }
            }

            Rectangle {
                id: workspaceTransitionOverlay
                objectName: "workspaceTransitionOverlay"
                visible: window.shellNavigationActivityVisible
                anchors.fill: parent
                z: 8
                color: "transparent"

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    hoverEnabled: true
                }
            }

            RowLayout {
                visible: false
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.bottomMargin: 8
                spacing: 18
                z: 1

                Text {
                    text: "Looksy"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 9
                    font.letterSpacing: 0
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 12
                    color: theme.alpha(theme.lineSoft, 0.62)
                }

                Text {
                    text: "Visual command environment"
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: 9
                    font.letterSpacing: 0
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 1
                    color: window.anyAppActivity ? window.primaryActivityAccent : theme.alpha(theme.vectorGreen, 0.58)
                }
            }
        }
    }

    Loader {
        id: preferencesDialogLoader
        active: false
        sourceComponent: preferencesDialogComponent
        onLoaded: {
            if (window.pendingPreferencesDialogPage >= 0 && item) {
                item.openAt(window.pendingPreferencesDialogPage)
                window.pendingPreferencesDialogPage = -1
            }
        }
    }

    Loader {
        id: firstRunGuideLoader
        active: appShell.firstRunExperiencePending || window.pendingFirstRunGuideOpen
        sourceComponent: firstRunGuideComponent
        onLoaded: {
            if (window.pendingFirstRunGuideOpen && !window.recoverySurfaceVisible) {
                item.start()
                window.pendingFirstRunGuideOpen = false
            }
        }
    }

    Loader {
        id: keyboardShortcutsDialogLoader
        active: false
        sourceComponent: keyboardShortcutsDialogComponent
        onLoaded: {
            if (window.pendingKeyboardShortcutsDialogOpen && item) {
                item.open()
                window.pendingKeyboardShortcutsDialogOpen = false
            }
        }
    }

    Loader {
        id: uiPrimitivesLoader
        active: false
        sourceComponent: Component {
            UiPrimitivesPage { transientParent: window; previewFontFamily: window.font.family }
        }
    }

    Loader {
        id: commandPaletteDialogLoader
        active: false
        sourceComponent: commandPaletteDialogComponent
        onLoaded: {
            if (window.pendingCommandPaletteDialogOpen && item) {
                item.open()
                window.pendingCommandPaletteDialogOpen = false
            }
        }
    }

    Loader {
        id: aboutDialogLoader
        active: false
        sourceComponent: aboutDialogComponent
        onLoaded: {
            if (window.pendingAboutDialogOpen && item) {
                item.open()
                window.pendingAboutDialogOpen = false
            }
        }
    }

    Component {
        id: preferencesDialogComponent

        AppPreferencesDialog {
            objectName: "preferencesDialog"
            parent: Overlay.overlay
        }
    }

    Component {
        id: firstRunGuideComponent

        FirstRunGuideDialog {
            parent: Overlay.overlay
            canOpenSelectedPhoto: window.canOpenSelectedPhotoInEdit()
            onSampleRequested: {
                const samplePath = appShell.prepareSampleProject()
                if (samplePath.length === 0) {
                    errorText = "The sample photos could not be prepared. Choose your own folder instead."
                    return
                }
                window.libraryWorkspaceMode = "catalog"
                window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
                window.catalogAssetModel.importFolderPath(samplePath)
            }
            onImportRequested: window.beginImportWorkspace()
            onLibraryRequested: window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
            onEditRequested: window.openSelectedPhotoInEdit()
            onExportRequested: window.switchToWorkspaceIndex(window.exportWorkspaceIndex)
            onGuideFinished: {
                appShell.completeFirstRunExperience()
                close()
            }
            onSkipped: {
                appShell.completeFirstRunExperience()
                close()
            }
        }
    }

    Component {
        id: keyboardShortcutsDialogComponent

        KeyboardShortcutsDialog {
            objectName: "keyboardShortcutsDialog"
            parent: Overlay.overlay
        }
    }

    Component {
        id: commandPaletteDialogComponent

        CommandPaletteDialog {
            objectName: "commandPaletteDialog"
            parent: Overlay.overlay
            requiredCommandLabels: [
                "Library",
                "Edit",
                "Carousel",
                "Timeline",
                "Export",
                "Adjust Tool",
                "Crop Tool",
                "Masks Tool",
                "Brush Tool",
                "Compare Tool",
                "Export Tool",
                "New Import",
                "New Timeline",
                "New Carousel",
                "Open Selected Photo in Edit",
                "Start Export",
                "Reset Adjustments",
                "Compare View",
                "Crop Overlay",
                "Select Tool",
                "Razor Tool",
                "Slip Tool",
                "Roll Tool",
                "Add Slide",
                "Export Carousel",
                "Keyboard Shortcuts",
                "Command Palette"
            ]
            commands: [
                { action: switchToLibraryAction, category: "Navigate", shortcutHint: macStyle ? "⌘1" : "Ctrl+1" },
                { action: switchToEditAction, category: "Navigate", shortcutHint: macStyle ? "⌘2" : "Ctrl+2" },
                { action: switchToCarouselAction, category: "Navigate", shortcutHint: macStyle ? "⌘3" : "Ctrl+3" },
                { action: switchToTimelineAction, category: "Navigate", shortcutHint: macStyle ? "⌘4" : "Ctrl+4" },
                { action: switchToExportAction, category: "Navigate", shortcutHint: macStyle ? "⌘5" : "Ctrl+5" },
                { action: newImportAction, category: "File", shortcutHint: photoWorkspaceActive ? "" : (macStyle ? "⇧⌘I" : "Ctrl+Shift+I") },
                { action: newTimelineAction, category: "File", shortcutHint: macStyle ? "⌘N" : "Ctrl+N" },
                { action: newCanvasAction, category: "File", shortcutHint: "" },
                { action: shootMatchAction, category: "Edit", shortcutHint: "" },
                { action: openSelectedInEditAction, category: "File", shortcutHint: macStyle ? "⌘E" : "Ctrl+E" },
                { action: openSelectedTimelineAction, category: "File", shortcutHint: "" },
                { action: saveTimelineAction, category: "File", shortcutHint: macStyle ? "⌘S" : "Ctrl+S" },
                { action: renderTimelineAction, category: "Export", shortcutHint: "" },
                { action: importFolderAction, category: "File", shortcutHint: macStyle ? "⌘O" : "Ctrl+O" },
                { action: refreshLibraryAction, category: "Library", shortcutHint: macStyle ? "⌘R" : "Ctrl+R" },
                { action: queueExportAction, category: "Export", shortcutHint: macStyle ? "⇧⌘E" : "Ctrl+Shift+E" },
                { action: undoEditAction, category: "Edit", shortcutHint: macStyle ? "⌘Z" : "Ctrl+Z" },
                { action: redoEditAction, category: "Edit", shortcutHint: macStyle ? "⇧⌘Z" : "Ctrl+Shift+Z" },
                { action: selectAllAction, category: "Selection", shortcutHint: macStyle ? "⌘A" : "Ctrl+A" },
                { action: deselectAction, category: "Selection", shortcutHint: macStyle ? "⇧⌘A" : "Ctrl+Shift+A" },
                { action: resetAdjustmentsAction, category: "Edit", shortcutHint: "" },
                { action: toggleCompareAction, category: "Edit", shortcutHint: "" },
                { action: toggleCropAction, category: "Edit", shortcutHint: "" },
                { action: adjustToolAction, category: "Tools", shortcutHint: macStyle ? "⌥⌘A" : "Ctrl+Alt+A" },
                { action: cropToolAction, category: "Tools", shortcutHint: macStyle ? "⌥⌘C" : "Ctrl+Alt+C" },
                { action: masksToolAction, category: "Tools", shortcutHint: macStyle ? "⌥⌘M" : "Ctrl+Alt+M" },
                { action: brushToolAction, category: "Tools", shortcutHint: macStyle ? "⌥⌘B" : "Ctrl+Alt+B" },
                { action: compareToolAction, category: "Tools", shortcutHint: macStyle ? "⌥⌘P" : "Ctrl+Alt+P" },
                { action: exportToolAction, category: "Tools", shortcutHint: macStyle ? "⌥⌘E" : "Ctrl+Alt+E" },
                { action: addSequenceMarkerAction, category: "Timeline", shortcutHint: "M" },
                { action: splitAtPlayheadAction, category: "Timeline", shortcutHint: macStyle ? "⌘\\" : "Ctrl+\\" },
                { action: removeSelectedClipAction, category: "Timeline", shortcutHint: "" },
                { action: openTimelineMediaBrowserAction, category: "Timeline", shortcutHint: macStyle ? "⇧⌘M" : "Ctrl+Shift+M" },
                { action: toggleTimelinePlaybackAction, category: "Timeline", shortcutHint: "Space" },
                { action: stepTimelineBackwardAction, category: "Timeline", shortcutHint: "Left" },
                { action: stepTimelineForwardAction, category: "Timeline", shortcutHint: "Right" },
                { action: addTimelineVideoTrackAction, category: "Timeline", shortcutHint: macStyle ? "⇧⌘T" : "Ctrl+Shift+T" },
                { action: zoomInTimelineAction, category: "Timeline", shortcutHint: macStyle ? "⌘=" : "Ctrl+=" },
                { action: zoomOutTimelineAction, category: "Timeline", shortcutHint: macStyle ? "⌘-" : "Ctrl+-" },
                { action: zoomToFitTimelineAction, category: "Timeline", shortcutHint: macStyle ? "⌘0" : "Ctrl+0" },
                { action: timelineSelectToolAction, category: "Timeline", shortcutHint: "V" },
                { action: timelineRazorToolAction, category: "Timeline", shortcutHint: window.configuredShortcut("timeline-razor", "C") },
                { action: timelineSlipToolAction, category: "Timeline", shortcutHint: "Y" },
                { action: timelineRollToolAction, category: "Timeline", shortcutHint: "N" },
                { action: timelineToggleSnapAction, category: "Timeline", shortcutHint: "S" },
                { action: timelineGoToStartAction, category: "Timeline", shortcutHint: "Home" },
                { action: timelineGoToEndAction, category: "Timeline", shortcutHint: "End" },
                { action: timelineDeleteClipAction, category: "Timeline", shortcutHint: "Backspace" },
                { action: timelineSelectAllAction, category: "Timeline", shortcutHint: macStyle ? "⌘A" : "Ctrl+A" },
                { action: toggleSourcePanelAction, category: "Timeline", shortcutHint: "" },
                { action: toggleEffectsPanelAction, category: "Timeline", shortcutHint: "" },
                { action: toggleRippleAction, category: "Timeline", shortcutHint: "" },
                { action: restoreTimelineLayoutAction, category: "Timeline", shortcutHint: "" },
                { action: carouselAddSlideAction, category: "Carousel", shortcutHint: macStyle ? "⇧⌘A" : "Ctrl+Shift+A" },
                { action: carouselRemoveSlideAction, category: "Carousel", shortcutHint: "" },
                { action: carouselMoveBackwardAction, category: "Carousel", shortcutHint: macStyle ? "⌘[" : "Ctrl+[" },
                { action: carouselMoveForwardAction, category: "Carousel", shortcutHint: macStyle ? "⌘]" : "Ctrl+]" },
                { action: carouselAssignSelectedMediaAction, category: "Carousel", shortcutHint: "" },
                { action: carouselExportAction, category: "Carousel", shortcutHint: macStyle ? "⇧⌘C" : "Ctrl+Shift+C" },
                { action: carouselFormat4x5Action, category: "Carousel", shortcutHint: "" },
                { action: carouselFormat5x4Action, category: "Carousel", shortcutHint: "" },
                { action: commandPaletteAction, category: "View", shortcutHint: macStyle ? "⌘K" : "Ctrl+K" },
                { action: preferencesAction, category: "App", shortcutHint: macStyle ? "⌘," : "Ctrl+," },
                { action: keyboardShortcutsAction, category: "Help", shortcutHint: "" },
                { action: uiPrimitivesAction, category: "Help", shortcutHint: "" },
                { action: checkForUpdatesAction, category: "Help", shortcutHint: "" },
                { action: aboutAction, category: "Help", shortcutHint: "" },
                { action: runtimePathsAction, category: "Help", shortcutHint: "" },
                { action: openAppDataFolderAction, category: "Help", shortcutHint: "" },
                { action: openLogFolderAction, category: "Help", shortcutHint: "" },
                { action: openBackupsFolderAction, category: "Help", shortcutHint: "" },
                { action: openCacheFolderAction, category: "Help", shortcutHint: "" },
                { action: clearFinishedExportsAction, category: "Export", shortcutHint: "" },
                { action: minimizeWindowAction, category: "Window", shortcutHint: "" },
                { action: zoomWindowAction, category: "Window", shortcutHint: "" },
                { action: bringWindowToFrontAction, category: "Window", shortcutHint: "" },
                { action: quitAction, category: "App", shortcutHint: macStyle ? "⌘Q" : "Ctrl+Q" }
            ]
        }
    }

    Component {
        id: aboutDialogComponent

        AppDialog {
            parent: Overlay.overlay
            modal: true
            width: Math.min(popupAvailableWidth, Math.max(1, 420))
            standardButtons: Dialog.Ok
            title: "About Looksy"

            background: PopupSurface {}

            contentItem: ColumnLayout {
                spacing: 12

                LooksyLetterhead {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    animated: true
                }

                Label {
                    Layout.fillWidth: true
                    text: "Version " + appShell.appVersion
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 11
                }

                Label {
                    Layout.fillWidth: true
                    text: "A native editing shell for library browsing, still-image adjustments, timeline editing, carousel layouts, and export queue management."
                    color: theme.textMain
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: "Runtime data is stored in your application support, cache, log, and backup folders shown in Preferences."
                    color: theme.textMuted
                    wrapMode: Text.WordWrap
                }

                ActionButton {
                    visible: window.macStyle && macosUpdater && macosUpdater.available
                    text: "Check for Updates"
                    compact: true
                    onClicked: {
                        aboutDialogLoader.item.close()
                        macosUpdater.checkForUpdates()
                    }
                }
            }
        }
    }


    Component {
        id: libraryView

        StudioWorkspace {
            onShootMatchRequested: window.openShootMatchDialog()
            onModelSettingsRequested: window.openPreferencesDialog(7)
            objectName: "libraryStudioWorkspace"
            canvasMediaSelectionMode: window.canvasMediaTarget !== null
            canvasMediaTargetName: window.canvasMediaTarget ? window.canvasMediaTarget.name : ""
            onCanvasMediaAddRequested: window.addLibrarySelectionToCanvas()
            onCanvasMediaCancelRequested: window.cancelCanvasMediaSelection()
            onReviewImportedRequested: window.libraryWorkspaceMode = "library"
            workspaceActive: window.currentWorkspace === window.libraryWorkspaceIndex
            onBoardDocumentActivated: function(documentId) { window.openSavedBoards(documentId) }
            onSceneDocumentActivated: entry => window.openSavedSceneDocument(entry)
            onSceneWorkstationRequested: {
                window.switchToWorkspaceIndex(window.sceneWorkspaceIndex)
                if (sceneWorkspaceLoader.item) sceneWorkspaceLoader.item.depthMotionActive = false
            }
            onNewBoardsRequested: window.createCarouselFromMenu()
            onNewTimelineRequested: window.createTimelineFromMenu()
            onTimelineDocumentActivated: function(documentPath, recoveryCandidateId) {
                if (window.carouselPhotoEditActive && !window.finishCarouselPhotoEdit(true)) return
                if (recoveryCandidateId.length > 0) {
                    window.pendingTimelineRecoveryRouteId = String(recoveryCandidateId)
                    window.pendingTimelineRecoveryWorkspace = window.activeWorkspaceItem()
                    if (!timelineDocument.openRecoveryCandidate(recoveryCandidateId))
                        window.pendingTimelineRecoveryRouteId = ""
                    if (window.pendingTimelineRecoveryRouteId.length === 0)
                        window.pendingTimelineRecoveryWorkspace = null
                    return
                }
                if (timelineDocument.loadTimeline(documentPath))
                    window.switchToWorkspaceIndex(window.timelineWorkspaceIndex)
            }
            page: "library"
            catalogAssets: window.catalogAssetModel
            importModeOpen: window.libraryWorkspaceMode === "import"

            Component.onCompleted: {
                window.catalogAssetModel.mediaCollectionFilter = "images"
                Qt.callLater(restoreLibraryDestination)
                if (window.catalogAssetModel.mediaTypeFilter === "timelines")
                    window.catalogAssetModel.mediaTypeFilter = "all"
                window.restorePrimaryStudioPanels(this)
                if (importModeOpen)
                    window.catalogAssetModel.refreshImportSources()
            }

            onImportModeOpenChanged: {
                window.libraryWorkspaceMode = importModeOpen ? "import" : "catalog"
                if (importModeOpen) {
                    window.logInstantQmlRoute("import", window.libraryWorkspaceIndex)
                    window.catalogAssetModel.refreshImportSources()
                } else {
                    window.logInstantQmlRoute("library", window.libraryWorkspaceIndex)
                }
            }

            onAssetActivated: function(destination) {
                if (window.canvasMediaTarget && ["edit", "masks", "compare"].indexOf(destination) >= 0) {
                    window.addLibrarySelectionToCanvas()
                    return
                }
                if (["edit", "masks", "compare"].indexOf(destination) >= 0) {
                    if (window.carouselPhotoEditActive && !window.finishCarouselPhotoEdit(true)) return
                    window.editBoardsActive = false
                    window.editorUsesLocalAsset = false
                }
                if (destination === "new-carousel") {
                    window.createCarouselFromMenu()
                    return
                }

                if (destination === "edit") {
                    if (window.carouselPhotoEditActive && !window.finishCarouselPhotoEdit(true)) return
                    window.editBoardsActive = false
                    window.editorUsesLocalAsset = false
                    window.loadSelectedAssetIntoEditor()
                    window.activatePrimaryEditTool("adjust")
                    return
                }

                if (destination === "masks") {
                    window.loadSelectedAssetIntoEditor()
                    window.activatePrimaryEditTool("masks")
                    return
                }

                if (destination === "compare") {
                    window.loadSelectedAssetIntoEditor()
                    window.activatePrimaryEditTool("compare")
                    return
                }

                if (destination === "export") {
                    window.switchToWorkspaceIndex(window.exportWorkspaceIndex)
                    return
                }

                if (window.catalogAssetModel.selectedMediaType === "timeline"
                        && window.catalogAssetModel.selectedTimelineDocumentPath.length > 0)
                    timelineDocument.loadTimeline(window.catalogAssetModel.selectedTimelineDocumentPath)

                window.switchToWorkspaceIndex(window.timelineWorkspaceIndex)
            }
            onProjectActivated: function(destination, projectName) {
                window.openProjectWorkspace(destination, projectName)
            }
            onLocalAssetActivated: function(filePath, previewSource, displayName) {
                if (window.carouselPhotoEditActive && !window.finishCarouselPhotoEdit(true)) return
                window.editBoardsActive = false
                window.carouselPhotoEditActive = false
                window.editorUsesLocalAsset = true
                window.localEditorAssetId = filePath
                window.localEditorFilePath = filePath
                window.localEditorPreviewSource = previewSource
                window.localEditorDisplayName = displayName
                window.localEditorAdjustmentPatchJson = ""
                window.loadLocalAssetIntoEditor()
                window.activatePrimaryEditTool("adjust")
            }
        }
    }

    Component {
        id: editView

        StudioWorkspace {
            onShootMatchRequested: window.openShootMatchDialog()
            onModelSettingsRequested: window.openPreferencesDialog(7)
            objectName: "editStudioWorkspace"
            onNewCanvasRequested: newCanvasAction.trigger()
            allowAddBoards: false
            editBoardMode: window.editBoardsActive
            editBoardController: carouselDocument
            onEditBoardPhotoTextLinkRequested: function(layerId, boardIds) {
                if (window.commitActiveEditBoard() &&
                        !carouselDocument.linkEditBoardPhotoText(layerId, qsTr("Shared text"), boardIds))
                    showEditActionFeedback(carouselDocument.status, true)
            }
            onEditBoardPhotoTextDetachRequested: function(layerId) {
                if (window.commitActiveEditBoard() && !carouselDocument.detachEditBoardPhotoText(layerId))
                    showEditActionFeedback(carouselDocument.status, true)
            }
            editBoardModel: window.editBoardsActive ? carouselDocument.slides : []
            editBoardIndex: carouselDocument.currentIndex
            editBoardDocumentId: carouselDocument.savedDocumentId
            editBoardDocumentName: carouselDocument.documentName
            editBoardSaveState: carouselDocument.saveState
            editBoardSaveError: window.editBoardSyncError || carouselDocument.saveError
            editBoardCanAdd: carouselDocument.canAddSlide
            editBoardExportBusy: carouselDocument.exportBusy
            editBoardSwitchBusy: window.switchingEditBoard || editorPreview.photoPaintBusy || editorPreview.brushStrokeActive
                || carouselDocument.editBoardImageBusy
            enabled: !carouselDocument.editBoardImageBusy
            canUndoEdit: window.canUndoPhotoWorkflow(false)
            canRedoEdit: window.canUndoPhotoWorkflow(true)
            undoEditLabel: textEditSessionActive ? qsTr("Text") : window.componentWorkspaceActive ? (window.componentWorkspaceController.controller ? window.componentWorkspaceController.controller.undoLabel : qsTr("Undo")) : window.photoWorkflowUndoEntries.length ? window.photoWorkflowUndoEntries[window.photoWorkflowUndoEntries.length - 1].label : window.editBoardsActive ? carouselDocument.undoText : editorPreview.undoLabel
            redoEditLabel: textEditSessionActive ? qsTr("Text") : window.componentWorkspaceActive ? (window.componentWorkspaceController.controller ? window.componentWorkspaceController.controller.redoLabel : qsTr("Redo")) : window.photoWorkflowRedoEntries.length ? window.photoWorkflowRedoEntries[window.photoWorkflowRedoEntries.length - 1].label : window.editBoardsActive ? carouselDocument.redoText : editorPreview.redoLabel
            undoEditHandler: function() { window.performPhotoUndo(false) }
            historyShortcutsHandledByHost: true
            redoEditHandler: function() { window.performPhotoUndo(true) }
            resetEditHandler: function() { return window.resetCurrentPhotoEdits() }
            onEditBoardSelected: function(index) { window.activateEditBoard(index) }
            onEditBoardAddRequested: function(extendCurrent) { window.addEditBoard(extendCurrent) }
            onEditBoardActionRequested: function(index, operation) { window.requestEditBoardAction(index, operation) }
            onEditBoardImageDropRequested: function(index, fileUrl, operation, placement) { window.requestEditBoardImageDropAtPoint(index, fileUrl, operation, placement) }
            onEditBoardMoveRequested: function(index, insertionIndex) { window.moveEditBoard(index, insertionIndex) }
            onEditBoardSaveRequested: window.commitActiveEditBoard()
            editBoardHasPhotoOrigin: String(carouselDocument.photoOrigin.assetId || "").length > 0
            onReturnToPhotoRequested: window.returnToPhoto(false)
            onAddBoardsRequested: window.addBoardsFromCurrentEdit()
            onBoardImageLibraryRequested: window.beginCanvasMediaSelection()
            onBoardVideoSequenceRequested: window.openCurrentBoardSequence([])
            onBoardVideoSequencePreviewRequested: window.previewCurrentBoardSequence()
            onBoardSequenceExportRequested: {
                if (!window.commitActiveEditBoard()) return
                carouselDocument.exportCarousel()
            }
            onBoardCanvasSizeRequested: {
                if (!window.commitActiveEditBoard()) return
                newCanvasDialog.resizing = true
                newCanvasDialog.setCanvasSize(carouselDocument.editCanvasSettings())
                newCanvasDialog.open()
            }
            page: "edit"
            editorControllerOverride: window.componentWorkspaceActive ? window.componentWorkspaceController.controller : null
            openPhotoFilePath: window.editorUsesLocalAsset ? window.localEditorFilePath : window.openedCatalogFilePath
            catalogAssets: window.catalogAssetModel
            editTool: window.resolvedEditTool()
            workspaceActive: window.photoWorkspaceActive &&
                !(window.componentWorkspaceActive && window.componentWorkspaceController.busy === true)

            Component.onCompleted: window.restorePrimaryStudioPanels(this)

            onAssetActivated: function(destination) {
                if (window.componentWorkspaceActive) {
                    if (["masks", "brush", "retouch"].indexOf(String(destination)) >= 0) {
                        window.activatePrimaryEditTool(String(destination))
                        return
                    }
                    window.showPhotoRecipeNotice(qsTr("Apply or cancel component contents before changing documents."), false)
                    return
                }
                if (destination === "edit") {
                    if (window.carouselPhotoEditActive && !window.finishCarouselPhotoEdit(true)) return
                    window.editBoardsActive = false
                    window.editorUsesLocalAsset = false
                    window.loadSelectedAssetIntoEditor()
                    window.activatePrimaryEditTool("adjust")
                    return
                }
                if (destination === "library") {
                    window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
                    return
                }

                if (destination === "masks") {
                    window.activatePrimaryEditTool("masks")
                    return
                }

                if (destination === "compare") {
                    window.activatePrimaryEditTool("compare")
                    return
                }

                if (destination === "export") {
                    window.switchToWorkspaceIndex(window.exportWorkspaceIndex)
                    return
                }
            }
        }
    }

}
