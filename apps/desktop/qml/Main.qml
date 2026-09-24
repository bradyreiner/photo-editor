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
    property var closeCoordinator: null
    property bool allowUnconfirmedExportClose: false
    onClosing: function(event) {
        if (exportJobs.pendingAdmissionCount > 0 || (!allowUnconfirmedExportClose && exportJobs.admissionError.length > 0)) {
            event.accepted = false
            exportAdmissionCloseDialog.open()
            return
        }
        if (closeCoordinator) event.accepted = closeCoordinator.handleWindowClose()
    }

    Popup {
        id: exportAdmissionCloseDialog
        objectName: "exportAdmissionCloseDialog"
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(480, parent.width - 32)
        x: (parent.width - width) / 2
        y: Math.max(16, (parent.height - height) / 2)
        padding: 20
        background: Rectangle { color: theme.ink900; radius: 18; border.color: theme.lineSoft }
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
                    visible: exportJobs.admissionError.length > 0
                    enabled: exportJobs.pendingAdmissionCount === 0 && exportJobs.historyMutationsEnabled
                    onClicked: exportJobs.retryUnconfirmedExports()
                }
                ActionButton {
                    Layout.fillWidth: true
                    text: "Close without confirmation"
                    visible: exportJobs.admissionError.length > 0
                    enabled: exportJobs.pendingAdmissionCount === 0
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

    property bool workspaceInitialized: false
    property int currentWorkspace: 0
    property string pendingEditInspectorTab: ""
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
    property bool workspaceTransitionActive: false
    property int pendingWorkspaceIndex: -1
    property string pendingEditToolActivation: ""
    property double qmlRouteStartedAtMs: 0
    property double qmlRouteComponentCreatedAtMs: 0
    property string qmlRouteSurface: ""
    property bool qmlRouteReadyLogged: false
    property double qmlFrameLastSampleAtMs: 0
    property int qmlFrameSampleCount: 0
    property int qmlFrameJankCount: 0
    property int qmlFrameDroppedCount: 0
    property int qmlFrameIdleGapCount: 0
    property real qmlFrameMaxIntervalMs: 0
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
    readonly property int carouselWorkspaceIndex: 2
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
    property bool recoveryDetailsExpanded: false
    readonly property var workspaceTitles: ["Library", "Edit", "Carousel", "Timeline", "Export", "Scene"]
    readonly property var workspaceKinds: ["library", "edit", "carousel", "timeline", "export", "scene"]
    readonly property string pageVisibilityAuditContract: "page-visibility-audit:v1|pages:library,import,edit,masks,carousel,timeline,export|buckets:always,contextual,one-expansion,hover-focus,diagnostic,preserve|runtime-active:true|functionality:preserved|major-shape:preserved"
    readonly property string activityProgressPlacementContract: "activity-progress-placement:v2|header-overlay:true|layout-shift:false|text:false|cancel-action:true"
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
    readonly property bool recoverySurfaceVisible: (appShell.sessionRecoveryAvailable
                                                     || (jobQueue && jobQueue.hasRecoveryReport)
                                                     || catalogRecoveryNeeded
                                                     || jobRecoveryNeeded
                                                     || appShell.catalogRepairConfirmationPending)
                                                    && !recoveryBannerVisible
    readonly property string recoverySurfaceSummary: {
        if (catalogRecoveryNeeded)
            return "The Library could not open. Your originals are unchanged."
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
        return currentWorkspace === editWorkspaceIndex && failed
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
    readonly property bool appActivityStatusCancelable: !appActivityErrorVisible
                                                        && !workspaceTransitionActive
                                                        && primaryActivityCancelable
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
        return (macStyle ? "Meta+" : "Ctrl+") + key
    }

    function openPreferencesDialog(pageIndex) {
        pendingPreferencesDialogPage = Math.max(0, Number(pageIndex || 0))
        preferencesDialogLoader.active = true
        if (preferencesDialogLoader.item) {
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
        return (macStyle ? "Meta+Shift+" : "Ctrl+Shift+") + key
    }

    function primaryAltShortcut(key) {
        return (macStyle ? "Meta+Alt+" : "Ctrl+Alt+") + key
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
        if (currentWorkspace === editWorkspaceIndex)
            return editWorkspaceLoader.item ? editWorkspaceLoader.item : null
        if (currentWorkspace === carouselWorkspaceIndex)
            return carouselWorkspaceLoader.item ? carouselWorkspaceLoader.item : null
        if (currentWorkspace === timelineWorkspaceIndex)
            return timelineWorkspaceLoader.item ? timelineWorkspaceLoader.item : null
        if (currentWorkspace === exportWorkspaceIndex)
            return exportWorkspaceLoader.item ? exportWorkspaceLoader.item : null
        return workspaceLoader.item ? workspaceLoader.item : null
    }

    function workspaceLoaded(index) {
        if (index === sceneWorkspaceIndex)
            return sceneWorkspaceLoaded
        if (index === libraryWorkspaceIndex)
            return libraryWorkspaceLoaded
        if (index === editWorkspaceIndex)
            return editWorkspaceLoaded
        if (index === carouselWorkspaceIndex)
            return carouselWorkspaceLoaded
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
            return carouselWorkspaceLoader.item
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
        return workspacePerformanceSurface(currentWorkspace, currentWorkspace === editWorkspaceIndex ? resolvedEditTool() : "")
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
            qmlFrameIdleGapCount += 1
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
        if (hasQmlPerformance() && hasFrameIssue) {
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
        qmlFrameIdleGapCount = 0
        qmlFrameMaxIntervalMs = 0
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

    function beginShellTransition(targetWorkspaceIndex, title, detail, accent, editToolName) {
        const workspaceIndex = Number(targetWorkspaceIndex)
        if (workspaceIndex < 0 || workspaceIndex >= workspaceTitles.length)
            return false

        // A previous route's completion must not clear a newer destination.
        workspaceTransitionFinishTimer.stop()
        pendingWorkspaceIndex = workspaceIndex
        pendingEditToolActivation = String(editToolName || "")
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
        workspaceTransitionCommitTimer.restart()
        return true
    }

    function finishShellTransitionSoon() {
        if (!workspaceTransitionActive)
            return
        workspaceTransitionFinishTimer.restart()
    }

    function completeShellTransition() {
        workspaceTransitionCommitTimer.stop()
        workspaceTransitionFinishTimer.stop()
        workspaceTransitionActive = false
        pendingWorkspaceIndex = -1
        pendingEditToolActivation = ""
        qmlRouteStartedAtMs = 0
        qmlRouteComponentCreatedAtMs = 0
        qmlRouteSurface = ""
        qmlRouteReadyLogged = false
        shellTransitionTitle = ""
        shellTransitionDetail = ""
    }

    function commitPendingWorkspaceTransition() {
        if (!workspaceTransitionActive) {
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
            Qt.callLater(function() {
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
        if (loadedWorkspaceItem(index) === null)
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
        if (currentWorkspace === editWorkspaceIndex)
            return editWorkspaceMode === "masks" || editWorkspaceMode === "brush" ? "masks" : "edit"
        if (currentWorkspace === carouselWorkspaceIndex)
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
        return currentWorkspace === timelineWorkspaceIndex ? activeWorkspaceItem() : null
    }

    function activeCarouselView() {
        return currentWorkspace === carouselWorkspaceIndex ? activeWorkspaceItem() : null
    }

    function activatePrimaryEditTool(toolName) {
        const cleanTool = String(toolName || "").trim().toLowerCase()
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
        const editWorkspace = currentWorkspace === editWorkspaceIndex ? activeWorkspaceItem() : null
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

    function switchWorkspaceByNumber(number) {
        return switchToWorkspaceIndex(Number(number) - 1)
    }

    function openEditPresetPanelFromExport(action) {
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

    readonly property bool photoRecipeWorkspace: currentWorkspace === libraryWorkspaceIndex
                                                || currentWorkspace === editWorkspaceIndex
    property bool applyingPhotoRecipe: false
    property string photoRecipeNoticeText: ""
    property bool photoRecipeNoticeUndo: false

    function canCopyPhotoRecipe() {
        return photoRecipeWorkspace && !copyPhotoEditsDialog.visible
                && !focusItemSupports("copy")
                && !(currentWorkspace === editWorkspaceIndex && editorUsesLocalAsset)
                && catalogAssets.selectedAssetId.length > 0
                && catalogAssets.selectedMediaType === "photo"
    }

    function canPastePhotoRecipe() {
        return photoRecipeWorkspace && !copyPhotoEditsDialog.visible
                && !focusItemSupports("paste")
                && !(currentWorkspace === editWorkspaceIndex && editorUsesLocalAsset)
                && (catalogAssets.selectedAssetCount > 0
                    || (catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedMediaType === "photo"))
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
        // Freeze the source before showing the chooser, including unsaved edits.
        copyPhotoEditsDialog.sourcePatch = currentWorkspace === editWorkspaceIndex
                && editorPreview.assetId === catalogAssets.selectedAssetId
                ? editorPreview.currentAssetBoundAdjustmentPatchJson()
                : catalogAssets.selectedAdjustmentPatchJson
        copyPhotoEditsDialog.sourceLabel = currentSelectionLabel()
        copyPhotoEditsDialog.errorText = ""
        copyPhotoEditsDialog.open()
    }

    function pastePhotoEdits() {
        if (!canPastePhotoRecipe()) return 0
        const targets = catalogAssets.selectedAssetIds.slice()
        if (targets.length === 0) targets.push(catalogAssets.selectedAssetId)
        const patch = editorPreview.copiedPhotoRecipePatchJson
        applyingPhotoRecipe = true
        let count = 0
        try {
            // Merge onto the latest live edit, not an older catalog snapshot.
            if (currentWorkspace === editWorkspaceIndex) syncEditorRecipeToCatalog()
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
        if (editorUsesLocalAsset && currentWorkspace === editWorkspaceIndex && localEditorFilePath.length > 0)
            return localEditorDisplayName.length > 0 ? localEditorDisplayName : baseNameFromPath(localEditorFilePath)

        if (currentWorkspace === timelineWorkspaceIndex && timelineDocument.savedRenderPath.length > 0)
            return timelineDocument.timelineName.length > 0 ? timelineDocument.timelineName : "Untitled Timeline"

        if (catalogAssets.selectedFilePath.length > 0) {
            const baseName = baseNameFromPath(catalogAssets.selectedFilePath)
            return baseName.length > 0 ? baseName : catalogAssets.selectedAssetId
        }

        return ""
    }

    function canOpenSelectedPhotoInEdit() {
        return (catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedMediaType === "photo")
                || catalogAssets.assetCount > 0
    }

    function canOpenSelectedTimeline() {
        return catalogAssets.selectedMediaType === "timeline"
                && catalogAssets.selectedTimelineDocumentPath.length > 0
    }

    function canSaveTimeline() {
        return currentWorkspace === timelineWorkspaceIndex && timelineDocument.clipCount > 0
    }

    function canOpenTimelineMediaBrowser() {
        return currentWorkspace === timelineWorkspaceIndex
    }

    function canControlTimelinePlayback() {
        return currentWorkspace === timelineWorkspaceIndex && timelineDocument.currentIndex >= 0
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

        if (editorUsesLocalAsset && currentWorkspace === editWorkspaceIndex && localEditorFilePath.length > 0) {
            return {
                "assetId": localEditorFilePath,
                "mediaType": "photo",
                "displayLabel": currentSelectionLabel(),
                "sourcePath": localEditorFilePath,
                "editedPreviewSource": editorPreview.previewSource,
                "adjustmentPatchJson": editorPreview.currentAssetBoundAdjustmentPatchJson()
            }
        }

        if (currentWorkspace === editWorkspaceIndex
                && catalogAssets.selectedAssetId.length > 0
                && catalogAssets.selectedMediaType === "photo"
                && catalogAssets.selectedFilePath.length > 0) {
            return {
                "assetId": catalogAssets.selectedAssetId,
                "mediaType": catalogAssets.selectedMediaType,
                "displayLabel": currentSelectionLabel(),
                "sourcePath": catalogAssets.selectedFilePath,
                "editedPreviewSource": editorPreview.previewSource,
                "adjustmentPatchJson": editorPreview.currentAssetBoundAdjustmentPatchJson()
            }
        }

        if (catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedFilePath.length > 0) {
            return {
                "assetId": catalogAssets.selectedAssetId,
                "mediaType": catalogAssets.selectedMediaType,
                "displayLabel": currentSelectionLabel(),
                "sourcePath": catalogAssets.selectedFilePath,
                "editedPreviewSource": editorPreview.previewSource,
                "adjustmentPatchJson": catalogAssets.selectedAdjustmentPatchJson
            }
        }

        return null
    }

    function canQueueExportFromMenu() {
        return currentExportPayload() !== null
    }

    function queueExportFromMenu() {
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

    function loadSelectedAssetIntoEditor() {
        if (catalogAssets.selectedAssetId.length === 0 && catalogAssets.assetCount > 0)
            catalogAssets.selectIndex(0)

        syncEditorRecipeToCatalog(catalogAssets.selectedAssetId)
        if (catalogAssets.selectedAssetId.length === 0 || catalogAssets.selectedMediaType !== "photo") {
            editorPreview.loadAsset(
                "",
                "",
                "",
                "",
                "",
                "",
                ""
            )
            return
        }

        editorPreview.loadAsset(
            catalogAssets.selectedAssetId,
            catalogAssets.selectedFilePath,
            catalogAssets.selectedThumbnailSource,
            catalogAssets.selectedCameraLabel,
            catalogAssets.selectedCaptureDate,
            catalogAssets.selectedLensModel,
            catalogAssets.selectedAdjustmentPatchJson
        )
    }

    function loadLocalAssetIntoEditor() {
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

    function beginCarouselPhotoEdit(payload) {
        const targetKey = String(payload.targetKey || "")
        const filePath = String(payload.filePath || "")
        if (targetKey.length === 0 || filePath.length === 0)
            return

        carouselPhotoEditActive = true
        carouselPhotoEditTargetKey = targetKey
        carouselPhotoEditDisplayName = String(payload.displayLabel || baseNameFromPath(filePath))
        carouselPhotoEditGeneration += 1
        editorUsesLocalAsset = true
        localEditorAssetId = targetKey + "|edit:" + carouselPhotoEditGeneration
        localEditorFilePath = filePath
        localEditorPreviewSource = String(payload.previewSource || "")
        localEditorDisplayName = carouselPhotoEditDisplayName
        localEditorAdjustmentPatchJson =
                String(payload.adjustmentPatchJson || "")
        beginShellTransition(editWorkspaceIndex,
                             "Editing Carousel Photo",
                             "Adjust the source photo, then apply it back to the same Carousel instance.",
                             workspaceAccent(editWorkspaceIndex),
                             "adjust")
    }

    function finishCarouselPhotoEdit(applyChanges) {
        if (!carouselPhotoEditActive)
            return
        if (applyChanges
                && !carouselDocument.applyAdjustmentPatchToPhotoEditTarget(
                       carouselPhotoEditTargetKey,
                       editorPreview.currentAssetBoundAdjustmentPatchJson()))
            return

        carouselPhotoEditActive = false
        carouselPhotoEditTargetKey = ""
        carouselPhotoEditDisplayName = ""
        editorUsesLocalAsset = false
        localEditorAssetId = ""
        localEditorFilePath = ""
        localEditorPreviewSource = ""
        localEditorDisplayName = ""
        localEditorAdjustmentPatchJson = ""
        beginShellTransition(carouselWorkspaceIndex,
                             applyChanges ? "Applied Photo Edit" : "Returning to Carousel",
                             applyChanges
                                 ? "The Carousel instance now uses the updated non-destructive recipe."
                                 : "The Carousel photo was left unchanged.",
                             workspaceAccent(carouselWorkspaceIndex),
                             "")
    }

    function syncEditorRecipeToCatalog(nextAssetId) {
        if (editorUsesLocalAsset)
            return

        const currentAssetId = editorPreview.assetId
        if (currentAssetId.length === 0)
            return

        if (nextAssetId !== undefined && nextAssetId.length > 0 && nextAssetId === currentAssetId)
            return

        // This is the same asset, not a reusable settings paste. Preserve its
        // layers, history and source/profile bindings as well as global edits.
        catalogAssets.applyAdjustmentPatchJsonToAssetIds(
                    [currentAssetId],
                    editorPreview.currentAssetBoundAdjustmentPatchJson())
    }

    function beginImportWorkspace() {
        libraryWorkspaceMode = "import"
        switchToWorkspaceIndex(libraryWorkspaceIndex)
        catalogAssets.refreshImportSources()
    }

    function openFirstRunGuide() {
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
        if (sessionRestoreApplied || !appShell.sessionRecoveryAvailable)
            return true
        if (catalogAssets.loading)
            return false

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
        if (currentWorkspace === editWorkspaceIndex)
            loadSelectedAssetIntoEditor()
        return true
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
        if (!canOpenSelectedPhotoInEdit())
            return

        if (catalogAssets.selectedAssetId.length === 0 && catalogAssets.assetCount > 0)
            catalogAssets.selectIndex(0)

        carouselPhotoEditActive = false
        carouselPhotoEditTargetKey = ""
        editorUsesLocalAsset = false
        beginShellTransition(editWorkspaceIndex,
                             "Opening Edit",
                             workspaceSubtitles[editWorkspaceIndex],
                             workspaceAccent(editWorkspaceIndex),
                             "adjust")
    }

    function openSelectedTimelineDocument() {
        if (!canOpenSelectedTimeline())
            return

        timelineDocument.loadTimeline(catalogAssets.selectedTimelineDocumentPath)
        switchToWorkspaceIndex(timelineWorkspaceIndex)
    }

    function createTimelineFromMenu() {
        timelineDocument.createTimeline()
        switchToWorkspaceIndex(timelineWorkspaceIndex)
    }

    function createCarouselFromMenu() {
        carouselDocument.createCarouselWithSettings({
            "carouselName": "Untitled Carousel",
            "format": "4x5",
            "slideCount": 1
        })
        switchToWorkspaceIndex(carouselWorkspaceIndex)
    }

    function saveCurrentTimeline() {
        const timelineView = activeTimelineView()
        if (timelineView && timelineView.saveTimelineToLibrary)
            timelineView.saveTimelineToLibrary()
    }

    function openProjectWorkspace(destination, projectName) {
        if (projectName.length === 0)
            return

        editorUsesLocalAsset = false
        catalogAssets.openProject(projectName)

        if (destination === "edit") {
            if (catalogAssets.selectedMediaType === "photo")
                loadSelectedAssetIntoEditor()
            else
                editorPreview.loadAsset("", "", "", "", "", "", "")
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
        shortcut: window.primaryShiftShortcut("I")
        onTriggered: window.beginImportWorkspace()
    }

    Action {
        id: newTimelineAction
        text: "New Timeline"
        shortcut: StandardKey.New
        onTriggered: window.createTimelineFromMenu()
    }

    Action {
        id: newCarouselAction
        text: "New Carousel"
        onTriggered: window.createCarouselFromMenu()
    }

    Action {
        id: openSelectedInEditAction
        text: "Open Selected In Edit"
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
        shortcut: appShell.shortcutSequence("queue-export", window.primaryShiftShortcut("E"))
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
        text: "Undo"
        shortcut: window.primaryShortcut("Z")
        enabled: (window.currentWorkspace === window.libraryWorkspaceIndex && catalogAssets.canUndoCulling)
                 || (window.currentWorkspace === window.editWorkspaceIndex && editorPreview.canUndo)
                 || (window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.canUndo)
        onTriggered: {
            if (window.currentWorkspace === window.libraryWorkspaceIndex)
                catalogAssets.undoCulling()
            else if (window.currentWorkspace === window.timelineWorkspaceIndex)
                timelineDocument.undo()
            else
                editorPreview.undo()
        }
    }

    Action {
        id: redoEditAction
        text: "Redo"
        shortcut: window.primaryShiftShortcut("Z")
        enabled: (window.currentWorkspace === window.libraryWorkspaceIndex && catalogAssets.canRedoCulling)
                 || (window.currentWorkspace === window.editWorkspaceIndex && editorPreview.canRedo)
                 || (window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.canRedo)
        onTriggered: {
            if (window.currentWorkspace === window.libraryWorkspaceIndex)
                catalogAssets.redoCulling()
            else if (window.currentWorkspace === window.timelineWorkspaceIndex)
                timelineDocument.redo()
            else
                editorPreview.redo()
        }
    }

    Action {
        id: copyPhotoEditsAction
        text: "Copy Edits…"
        shortcut: window.primaryShortcut("C")
        enabled: window.canCopyPhotoRecipe()
        onTriggered: window.openCopyPhotoEdits()
    }
    Action {
        id: pastePhotoEditsAction
        text: "Paste Edits"
        shortcut: window.primaryShortcut("V")
        enabled: window.canPastePhotoRecipe()
        onTriggered: window.pastePhotoEdits()
    }
    Shortcut {
        sequence: "Ctrl+C"
        enabled: window.macStyle && copyPhotoEditsAction.enabled
        onActivated: window.openCopyPhotoEdits()
    }
    Shortcut {
        sequence: "Ctrl+V"
        enabled: window.macStyle && pastePhotoEditsAction.enabled
        onActivated: window.pastePhotoEdits()
    }
    Action {
        id: undoPastedPhotoEditsAction
        text: "Undo Pasted Edits"
        enabled: window.photoRecipeWorkspace && catalogAssets.canUndoBatchRecipe
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
        text: "Reset Adjustments"
        enabled: editorPreview.hasAsset
        onTriggered: editorPreview.resetAdjustments()
    }

    Action {
        id: toggleCompareAction
        text: "Compare View"
        checkable: true
        checked: editorPreview.compareMode
        enabled: editorPreview.hasAsset
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
        enabled: editorPreview.hasAsset
        onTriggered: editorPreview.cropEditing = !editorPreview.cropEditing
    }

    Action {
        id: adjustToolAction
        text: "Adjust Tool"
        shortcut: appShell.shortcutSequence("tool-adjust", window.primaryAltShortcut("A"))
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("adjust")
    }

    Action {
        id: cropToolAction
        text: "Crop Tool"
        shortcut: appShell.shortcutSequence("tool-crop", window.primaryAltShortcut("C"))
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("crop")
    }

    Action {
        id: masksToolAction
        text: "Masks Tool"
        shortcut: appShell.shortcutSequence("tool-masks", window.primaryAltShortcut("M"))
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("masks")
    }

    Action {
        id: brushToolAction
        text: "Brush Tool"
        shortcut: appShell.shortcutSequence("tool-brush", window.primaryAltShortcut("B"))
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("brush")
    }

    Action {
        id: compareToolAction
        text: "Compare Tool"
        shortcut: appShell.shortcutSequence("tool-compare", window.primaryAltShortcut("P"))
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("compare")
    }

    Action {
        id: exportToolAction
        text: "Export Tool"
        shortcut: appShell.shortcutSequence("tool-export", window.primaryAltShortcut("E"))
        enabled: catalogAssets.selectedAssetId.length > 0 || editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("export")
    }

    Action {
        id: addSequenceMarkerAction
        text: "Add Sequence Marker"
        shortcut: "M"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.clipCount > 0
        onTriggered: timelineDocument.addSequenceMarker()
    }

    Action {
        id: splitAtPlayheadAction
        text: "Split At Playhead"
        shortcut: window.primaryShortcut("\\")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.currentIndex >= 0
        onTriggered: {
            const timelineView = window.activeTimelineView()
            if (timelineView && timelineView.splitAtPlayhead)
                timelineView.splitAtPlayhead()
        }
    }

    Action {
        id: removeSelectedClipAction
        text: "Remove Selected Clip"
        shortcut: StandardKey.Delete
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
        shortcut: "Space"
        enabled: window.canControlTimelinePlayback()
        onTriggered: timelinePlayback.togglePlayback()
    }

    Action {
        id: stepTimelineBackwardAction
        text: "Step Back One Frame"
        shortcut: "Left"
        enabled: window.canControlTimelinePlayback()
        onTriggered: timelinePlayback.stepFrames(-1)
    }

    Action {
        id: stepTimelineForwardAction
        text: "Step Forward One Frame"
        shortcut: "Right"
        enabled: window.canControlTimelinePlayback()
        onTriggered: timelinePlayback.stepFrames(1)
    }

    Action {
        id: addTimelineVideoTrackAction
        text: "Add Video Track"
        shortcut: window.primaryShiftShortcut("T")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.addVideoTrack()
    }

    Action {
        id: zoomInTimelineAction
        text: "Zoom In Timeline"
        shortcut: window.primaryShortcut("=")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: {
            const tv = window.activeTimelineView()
            if (tv) tv.setTimelineZoom(tv.timelineZoom * 1.25, tv.timelineZoom * 0)
        }
    }

    Action {
        id: zoomOutTimelineAction
        text: "Zoom Out Timeline"
        shortcut: window.primaryShortcut("-")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: {
            const tv = window.activeTimelineView()
            if (tv) tv.setTimelineZoom(tv.timelineZoom / 1.25, 0)
        }
    }

    Action {
        id: zoomToFitTimelineAction
        text: "Zoom To Fit Timeline"
        shortcut: window.primaryShortcut("0")
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: {
            const tv = window.activeTimelineView()
            if (tv) tv.zoomToFit()
        }
    }

    Action {
        id: timelineSelectToolAction
        text: "Select Tool"
        shortcut: "V"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.activeTool = "select"
    }

    Action {
        id: timelineRazorToolAction
        text: "Razor Tool"
        shortcut: "B"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.activeTool = "razor"
    }

    Action {
        id: timelineSlipToolAction
        text: "Slip Tool"
        shortcut: "Y"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.activeTool = "slip"
    }

    Action {
        id: timelineRollToolAction
        text: "Roll Tool"
        shortcut: "N"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelineDocument.activeTool = "roll"
    }

    Action {
        id: timelineToggleSnapAction
        text: "Toggle Snapping"
        shortcut: "S"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: {
            const tv = window.activeTimelineView()
            if (tv) tv.snappingEnabled = !tv.snappingEnabled
        }
    }

    Action {
        id: timelineGoToStartAction
        text: "Go To Start"
        shortcut: "Home"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelinePlayback.seekSequenceSeconds(0)
    }

    Action {
        id: timelineGoToEndAction
        text: "Go To End"
        shortcut: "End"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: timelinePlayback.seekSequenceSeconds(timelineDocument.totalDurationSeconds)
    }

    Action {
        id: timelineDeleteClipAction
        text: "Delete Selected Clip"
        shortcut: "Backspace"
        enabled: window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.currentIndex >= 0
        onTriggered: timelineDocument.removeSelectedClip()
    }

    Action {
        id: timelineDropMarkerAction
        text: "Drop Marker"
        shortcut: "M"
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
        text: "Add Slide"
        shortcut: window.primaryShiftShortcut("A")
        enabled: window.currentWorkspace === window.carouselWorkspaceIndex && carouselDocument.canAddSlide
        onTriggered: carouselDocument.addSlide()
    }

    Action {
        id: carouselRemoveSlideAction
        text: "Remove Current Slide"
        shortcut: StandardKey.Delete
        enabled: window.currentWorkspace === window.carouselWorkspaceIndex && carouselDocument.slideCount > 0
        onTriggered: carouselDocument.removeCurrentSlide()
    }

    Action {
        id: carouselMoveBackwardAction
        text: "Move Slide Earlier"
        shortcut: window.primaryShortcut("[")
        enabled: window.currentWorkspace === window.carouselWorkspaceIndex && carouselDocument.slideCount > 1
        onTriggered: carouselDocument.moveCurrentBackward()
    }

    Action {
        id: carouselMoveForwardAction
        text: "Move Slide Later"
        shortcut: window.primaryShortcut("]")
        enabled: window.currentWorkspace === window.carouselWorkspaceIndex && carouselDocument.slideCount > 1
        onTriggered: carouselDocument.moveCurrentForward()
    }

    Action {
        id: carouselAssignSelectedMediaAction
        text: "Assign Selected Library Media"
        enabled: window.canAssignSelectedMediaToCarousel()
        onTriggered: window.assignSelectedMediaToCarousel()
    }

    Action {
        id: carouselExportAction
        text: "Export Carousel"
        shortcut: window.primaryShiftShortcut("C")
        enabled: window.currentWorkspace === window.carouselWorkspaceIndex && !carouselDocument.exportBusy
        onTriggered: carouselDocument.exportCarousel()
    }

    Action {
        id: carouselFormat4x5Action
        text: "4x5 Portrait"
        checkable: true
        checked: carouselDocument.documentFormat === "4x5"
        enabled: window.currentWorkspace === window.carouselWorkspaceIndex
        onTriggered: carouselDocument.documentFormat = "4x5"
    }

    Action {
        id: carouselFormat5x4Action
        text: "5x4 Landscape"
        checkable: true
        checked: carouselDocument.documentFormat === "5x4"
        enabled: window.currentWorkspace === window.carouselWorkspaceIndex
        onTriggered: carouselDocument.documentFormat = "5x4"
    }

    Action {
        id: switchToLibraryAction
        text: "Library"
        shortcut: appShell.shortcutSequence("workspace-library", window.primaryShortcut("1"))
        checkable: true
        checked: window.currentWorkspace === window.libraryWorkspaceIndex
        onTriggered: window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
    }

    Action {
        id: switchToEditAction
        text: "Edit"
        shortcut: appShell.shortcutSequence("workspace-edit", window.primaryShortcut("2"))
        checkable: true
        checked: window.currentWorkspace === window.editWorkspaceIndex
        onTriggered: window.switchToWorkspaceIndex(window.editWorkspaceIndex)
    }

    Action {
        id: switchToCarouselAction
        text: "Carousel"
        shortcut: window.primaryShortcut("3")
        checkable: true
        checked: window.currentWorkspace === window.carouselWorkspaceIndex
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
        onTriggered: window.switchToWorkspaceIndex(window.exportWorkspaceIndex)
    }

    Action {
        id: toggleSourcePanelAction
        text: "Show Source Panel"
        checkable: true
        checked: window.activeTimelineView() !== null && !window.activeTimelineView().sourcePanelCollapsed
        enabled: window.activeTimelineView() !== null
        onTriggered: {
            const timelineView = window.activeTimelineView()
            if (timelineView && timelineView.toggleSourcePanel)
                timelineView.toggleSourcePanel()
        }
    }

    Action {
        id: toggleEffectsPanelAction
        text: "Show Effects Panel"
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
        shortcut: appShell.shortcutSequence("command-palette", window.primaryShortcut("K"))
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
        Menu {
            title: "&File"
            MenuItem { action: newImportAction }
            MenuItem { action: newTimelineAction }
            MenuItem { action: newCarouselAction }
            MenuSeparator {}
            MenuItem { action: openSelectedInEditAction }
            MenuItem { action: openSelectedTimelineAction }
            MenuSeparator {}
            MenuItem { action: saveTimelineAction }
            MenuItem { action: importFolderAction }
            MenuItem { action: refreshLibraryAction }
            MenuSeparator {}
            MenuItem { action: queueExportAction }
            MenuSeparator {}
            MenuItem { action: preferencesAction }
            MenuItem { action: quitAction }
        }

        Menu {
            title: "&Edit"
            MenuItem { action: undoEditAction }
            MenuItem { action: redoEditAction }
            MenuItem { action: undoPastedPhotoEditsAction }
            MenuSeparator {}
            MenuItem { action: copyPhotoEditsAction }
            MenuItem { action: pastePhotoEditsAction }
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
            MenuItem { action: toggleSourcePanelAction }
            MenuItem { action: toggleEffectsPanelAction }
            MenuItem { action: toggleSnappingAction }
            MenuItem { action: toggleRippleAction }
            MenuItem { action: restoreTimelineLayoutAction }
        }

        Menu {
            title: "&Window"
            MenuItem { action: minimizeWindowAction }
            MenuItem { action: zoomWindowAction }
            MenuItem { action: bringWindowToFrontAction }
        }

        Menu {
            title: "&Timeline"
            enabled: window.currentWorkspace === window.timelineWorkspaceIndex
            MenuItem { action: newTimelineAction }
            MenuItem { action: saveTimelineAction }
            MenuSeparator {}
            MenuItem { action: openTimelineMediaBrowserAction }
            MenuItem { action: toggleTimelinePlaybackAction }
            MenuItem { action: stepTimelineBackwardAction }
            MenuItem { action: stepTimelineForwardAction }
            MenuSeparator {}
            MenuItem { action: addSequenceMarkerAction }
            MenuItem { action: splitAtPlayheadAction }
            MenuItem { action: addTimelineVideoTrackAction }
            MenuItem { action: removeSelectedClipAction }
        }

        Menu {
            title: "Ca&rousel"
            enabled: window.currentWorkspace === window.carouselWorkspaceIndex
            MenuItem { action: newCarouselAction }
            MenuSeparator {}
            MenuItem { action: carouselAddSlideAction }
            MenuItem { action: carouselRemoveSlideAction }
            MenuItem { action: carouselMoveBackwardAction }
            MenuItem { action: carouselMoveForwardAction }
            MenuSeparator {}
            MenuItem { action: carouselAssignSelectedMediaAction }
            MenuSeparator {}
            MenuItem { action: carouselFormat4x5Action }
            MenuItem { action: carouselFormat5x4Action }
            MenuSeparator {}
            MenuItem { action: carouselExportAction }
        }

        Menu {
            title: "&View"
            MenuItem { action: commandPaletteAction }
        }

        Menu {
            title: "&Help"
            MenuItem { action: quickStartAction }
            MenuItem { action: keyboardShortcutsAction }
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
        appShell.currentWorkspace = currentWorkspace
        if (currentWorkspace === libraryWorkspaceIndex) {
            libraryWorkspaceLoaded = true
        } else if (currentWorkspace === editWorkspaceIndex) {
            editWorkspaceLoaded = true
        } else if (currentWorkspace === carouselWorkspaceIndex) {
            carouselWorkspaceLoaded = true
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

    onCurrentWorkspaceChanged: {
        if (workspaceInitialized)
            appShell.currentWorkspace = currentWorkspace

        if (currentWorkspace !== editWorkspaceIndex)
            syncEditorRecipeToCatalog()

        if (currentWorkspace === libraryWorkspaceIndex) {
            libraryWorkspaceLoaded = true
            Qt.callLater(function() {
                window.restorePrimaryStudioPanels(workspaceLoader.item)
            })
        } else if (currentWorkspace === editWorkspaceIndex) {
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
        } else if (currentWorkspace === carouselWorkspaceIndex) {
            carouselWorkspaceLoaded = true
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
        id: editWorkspacePrewarmTimer
        interval: 700
        repeat: false
        onTriggered: {
            if (!window.workspaceTransitionActive && !window.anyAppActivity && window.currentWorkspace !== window.sceneWorkspaceIndex)
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
            if (!window.workspaceTransitionActive && !window.anyAppActivity && window.currentWorkspace !== window.sceneWorkspaceIndex)
                window.preloadNextSecondaryWorkspace()
            else
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

    Timer {
        id: workspaceTransitionCommitTimer
        interval: 45
        repeat: false
        onTriggered: window.commitPendingWorkspaceTransition()
    }

    Timer {
        id: workspaceTransitionFinishTimer
        interval: 110
        repeat: false
        onTriggered: window.completeShellTransition()
    }

    CopyPhotoEditsDialog {
        id: copyPhotoEditsDialog
        parent: Overlay.overlay
        onCopyRequested: (patch, groups) => {
            if (editorPreview.copyPhotoRecipe(patch, groups)) {
                close()
                window.showPhotoRecipeNotice("Edits copied. Select photos and paste.", false)
            } else {
                errorText = "These edits could not be copied. Choose valid edit groups and try again."
            }
        }
    }
    Popup {
        id: photoRecipeNotice
        objectName: "photoRecipeNotice"
        parent: Overlay.overlay
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? parent.height - height - 32 : 0
        width: Math.min(520, parent ? parent.width - 32 : 520)
        padding: 14
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: theme.ink800; radius: 12; border.color: theme.lineSoft }
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

        function onPhotoRecipeChanged(assetId, filePath, previousPatch, nextPatch, restoringHistory) {
            editorPreview.applyCatalogRecipeChange(assetId, filePath, previousPatch, nextPatch, restoringHistory)
        }

        function onSelectedAssetChanged() {
            if (window.applyingPhotoRecipe) return
            window.editorUsesLocalAsset = false
            if (window.currentWorkspace === window.editWorkspaceIndex)
                window.loadSelectedAssetIntoEditor()
        }

        function onLoadingChanged() {
            if (appShell.sessionRecoveryAvailable && !window.sessionRestoreApplied)
                recoveryRestoreTimer.restart()
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
                spacing: 18

                BrandWordmark {
                    Layout.preferredWidth: 240
                    Layout.preferredHeight: implicitHeight
                    Layout.alignment: Qt.AlignVCenter
                    compact: true
                    showMark: true
                    showDescriptor: false
                }

                RowLayout {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: true
                    spacing: 18

                    Item { Layout.fillWidth: true }

                    Repeater {
                        model: workspaceTitles

                        delegate: Button {
                            id: workspaceTab

                            required property int index
                            required property string modelData

                            objectName: "workspaceTab" + workspaceTab.modelData
                            text: workspaceTab.modelData
                            Layout.preferredWidth: Math.max(theme.navItemMinWidth, tabLabel.implicitWidth + 18)
                            Layout.preferredHeight: theme.navHeight
                            flat: true
                            hoverEnabled: true
                            focusPolicy: Qt.TabFocus
                            activeFocusOnTab: true
                            ToolTip.visible: hovered
                            ToolTip.delay: 350
                            ToolTip.timeout: 2800
                            ToolTip.text: workspaceSubtitles[index]
                            onClicked: window.switchToWorkspaceIndex(index)

                            background: Rectangle {
                                anchors.fill: parent
                                anchors.topMargin: 9
                                anchors.bottomMargin: 9
                                radius: 2
                                color: workspaceTab.down ? theme.surfaceInset
                                       : workspaceTab.index === window.currentWorkspace ? theme.ink750
                                       : workspaceTab.hovered ? theme.ink850 : "transparent"
                                border.width: workspaceTab.visualFocus ? theme.focusBorderWidth : 0
                                border.color: theme.focusRing
                            }

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
                                    font.pixelSize: 12
                                    font.weight: workspaceTab.index === window.currentWorkspace ? Font.DemiBold : Font.Normal
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

                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 250
                    spacing: 3

                    Text {
                        objectName: "topSelectionStatusLabel"
                        Layout.fillWidth: true
                        text: window.currentSelectionLabel().length > 0
                              ? window.currentSelectionLabel()
                              : "No active asset"
                        color: window.currentSelectionLabel().length > 0 ? theme.textMain : theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        font.letterSpacing: 0
                        horizontalAlignment: Text.AlignRight
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 7
                        elide: Text.ElideNone
                    }

                    Text {
                        objectName: "topWorkspaceStatusLabel"
                        Layout.fillWidth: true
                        text: window.workspaceTitles[window.currentWorkspace] + " / v" + appShell.appVersion
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: 9
                        font.letterSpacing: 0
                        horizontalAlignment: Text.AlignRight
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 7
                        elide: Text.ElideNone
                    }
                }
            }

            ProgressStrip {
                objectName: "additiveActivityProgressLine"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 4
                visible: window.appActivityStatusVisible && !window.appActivityErrorVisible
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
                visible: window.appActivityStatusVisible && !window.appActivityErrorVisible
                         && !window.recoveryBannerVisible && window.appActivityStatusCancelable
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
                anchors.margins: 6
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
                anchors.margins: 6
                active: window.editWorkspaceLoaded || window.currentWorkspace === window.editWorkspaceIndex
                visible: window.currentWorkspace === window.editWorkspaceIndex
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

            Connections {
                target: editWorkspaceLoader.item
                enabled: editWorkspaceLoader.item !== null
                ignoreUnknownSignals: true

                function onAssetActivated(destination) {
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
                value: window.currentWorkspace === window.editWorkspaceIndex
                when: editWorkspaceLoader.item !== null
                restoreMode: Binding.RestoreBindingOrValue
            }

            Loader {
                id: carouselWorkspaceLoader
                anchors.fill: parent
                anchors.margins: 6
                active: window.carouselWorkspaceLoaded || window.currentWorkspace === window.carouselWorkspaceIndex
                visible: window.currentWorkspace === window.carouselWorkspaceIndex
                source: Qt.resolvedUrl("workspaces/CarouselView.qml")
                onLoaded: {
                    window.noteWorkspaceComponentCreated(window.carouselWorkspaceIndex)
                    window.markWorkspaceLoaded(window.carouselWorkspaceIndex)
                    window.maybeFinishWorkspaceTransition(window.carouselWorkspaceIndex)
                }
            }

            Connections {
                target: carouselWorkspaceLoader.item
                enabled: carouselWorkspaceLoader.item !== null
                ignoreUnknownSignals: true

                function onEditPhotoRequested(payload) {
                    window.beginCarouselPhotoEdit(payload)
                }
            }

            Loader {
                id: timelineWorkspaceLoader
                anchors.fill: parent
                anchors.margins: window.timelineProgramFullScreenActive ? 0 : 6
                active: window.timelineWorkspaceLoaded || window.currentWorkspace === window.timelineWorkspaceIndex
                visible: window.currentWorkspace === window.timelineWorkspaceIndex
                source: Qt.resolvedUrl("workspaces/TimelineView.qml")
                onLoaded: {
                    window.noteWorkspaceComponentCreated(window.timelineWorkspaceIndex)
                    window.markWorkspaceLoaded(window.timelineWorkspaceIndex)
                    window.maybeFinishWorkspaceTransition(window.timelineWorkspaceIndex)
                }
            }

            Loader {
                id: exportWorkspaceLoader
                anchors.fill: parent
                anchors.margins: 6
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
                id: carouselPhotoEditBar
                objectName: "carouselPhotoEditReturnBar"
                visible: window.carouselPhotoEditActive
                         && window.currentWorkspace === window.editWorkspaceIndex
                         && !window.shellNavigationActivityVisible
                z: 7
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 14
                width: Math.min(720, parent.width - 48)
                height: carouselPhotoEditBarContent.implicitHeight + 18
                radius: 9
                color: theme.alpha(theme.ink900, 0.96)
                border.width: 1
                border.color: theme.alpha(theme.accentCool400, 0.62)

                RowLayout {
                    id: carouselPhotoEditBarContent
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 10

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: width / 2
                        color: theme.accentCool400
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Label {
                            Layout.fillWidth: true
                            text: "Editing for Carousel"
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideNone
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 8
                        }

                        Label {
                            Layout.fillWidth: true
                            text: window.carouselPhotoEditDisplayName
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 9
                            elide: Text.ElideNone
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 7
                        }
                    }

                    ActionButton {
                        compact: true
                        text: "Return Without Applying"
                        tooltipText: "Discard this round-trip and leave the Carousel instance unchanged."
                        onClicked: window.finishCarouselPhotoEdit(false)
                    }

                    ActionButton {
                        objectName: "applyCarouselPhotoEditButton"
                        compact: true
                        text: "Apply & Return"
                        tone: "primary"
                        tooltipText: "Apply this non-destructive recipe to the same Carousel photo and return."
                        onClicked: window.finishCarouselPhotoEdit(true)
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
                "Open Selected In Edit",
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
                { action: newImportAction, category: "File", shortcutHint: macStyle ? "⇧⌘I" : "Ctrl+Shift+I" },
                { action: newTimelineAction, category: "File", shortcutHint: macStyle ? "⌘N" : "Ctrl+N" },
                { action: newCarouselAction, category: "File", shortcutHint: "" },
                { action: openSelectedInEditAction, category: "File", shortcutHint: macStyle ? "⌘E" : "Ctrl+E" },
                { action: openSelectedTimelineAction, category: "File", shortcutHint: "" },
                { action: saveTimelineAction, category: "File", shortcutHint: macStyle ? "⌘S" : "Ctrl+S" },
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
                { action: timelineRazorToolAction, category: "Timeline", shortcutHint: "B" },
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

        Dialog {
            parent: Overlay.overlay
            modal: true
            width: 420
            standardButtons: Dialog.Ok
            title: "About Looksy"

            background: Rectangle {
                radius: theme.radius16
                color: theme.ink900
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.85)
            }

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
                    font.family: theme.fontMono
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

    PopupSurface {
        id: recoverySurface
        objectName: "recoverySummarySurface"
        visible: window.recoverySurfaceVisible
        z: 500
        anchors.top: parent.top
        anchors.topMargin: theme.scaled(78)
        anchors.right: parent.right
        anchors.rightMargin: theme.scaled(18)
        width: Math.min(parent.width - theme.scaled(36),
                        theme.scaled(window.catalogRecoveryNeeded ? 480 : 380))
        height: Math.max(theme.scaled(80), recoverySurfaceColumn.implicitHeight + theme.scaled(16))
        radius: theme.radius16
        opacity: visible ? 1 : 0
        scale: visible ? 1 : 0.97
        transformOrigin: Item.TopRight
        Behavior on opacity { NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: theme.durationMedium; easing.type: Easing.OutBack } }
        accent: window.catalogRecoveryNeeded || window.jobRecoveryNeeded ? theme.error : theme.success
        accentRail: true
        fillColor: theme.ink850
        borderColor: theme.alpha(theme.glassEdge, 0.72)
        shadowColor: visible ? theme.alpha(theme.ink950, 0.56) : "transparent"

        ColumnLayout {
            id: recoverySurfaceColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: theme.scaled(8)
            spacing: theme.scaled(6)

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.scaled(8)

                AppIcon {
                    name: window.catalogRecoveryNeeded || window.jobRecoveryNeeded ? "info" : "check"
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    color: window.catalogRecoveryNeeded || window.jobRecoveryNeeded ? theme.error : theme.success
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.scaled(1)

                    Label {
                        objectName: "recoverySummaryTitle"
                        Layout.fillWidth: true
                        text: window.catalogRecoveryNeeded ? "Catalog recovery"
                              : window.jobRecoveryNeeded ? "Job recovery needs attention" : "Work recovered"
                        color: theme.textMain
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: window.recoverySurfaceSummary
                        color: theme.textMuted
                        font.pixelSize: theme.typeCaption
                        maximumLineCount: 1
                        elide: Text.ElideRight
                    }
                }

                ActionButton {
                    objectName: "recoveryDoneButton"
                    visible: !window.catalogRecoveryNeeded && !window.jobRecoveryNeeded && !appShell.catalogRepairConfirmationPending
                    iconName: "close"
                    showText: false
                    compact: true
                    tone: "tertiary"
                    tooltipText: "Dismiss recovery notice"
                    Accessible.name: "Dismiss recovery summary"
                    onClicked: window.dismissRecoverySurface()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.scaled(6)

                ActionButton {
                    objectName: "recoveryDetailsButton"
                    text: window.recoveryDetailsExpanded ? "Hide details" : "Details"
                    compact: true
                    tone: "tertiary"
                    Accessible.name: text
                    onClicked: window.recoveryDetailsExpanded = !window.recoveryDetailsExpanded
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    objectName: "recoveryReviewWorkButton"
                    visible: !window.catalogRecoveryNeeded
                    text: window.jobRecoveryNeeded ? "Review jobs" : "Review work"
                    compact: true
                    tone: "primary"
                    Accessible.name: window.jobRecoveryNeeded ? "Review paused background jobs" : "Review recovered work"
                    onClicked: window.reviewRecoveredWork()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: window.recoveryDetailsExpanded
                spacing: theme.scaled(4)

                Repeater {
                    model: window.recoveryDetailItems()
                    delegate: Label {
                        required property string modelData
                        Layout.fillWidth: true
                        text: "• " + modelData
                        color: theme.textMuted
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                    }
                }
            }

            RowLayout {
                objectName: "catalogRecoveryActions"
                Layout.fillWidth: true
                visible: window.catalogRecoveryNeeded || appShell.catalogRepairConfirmationPending
                spacing: theme.scaled(6)

                Item { Layout.fillWidth: true }

                ActionButton {
                    objectName: "catalogRetryButton"
                    visible: window.catalogRecoveryNeeded
                    text: "Retry"
                    compact: true
                    Accessible.name: "Retry opening the catalog"
                    onClicked: catalogAssets.refresh()
                }

                ActionButton {
                    objectName: "catalogBackupButton"
                    visible: window.catalogRecoveryNeeded
                    text: "Create backup"
                    compact: true
                    Accessible.name: "Create a catalog backup"
                    onClicked: appShell.createCatalogBackup()
                }

                ActionButton {
                    objectName: "catalogRepairButton"
                    visible: window.catalogRecoveryNeeded && !appShell.catalogRepairConfirmationPending
                    text: "Repair from backup"
                    compact: true
                    Accessible.name: "Review catalog repair"
                    onClicked: appShell.requestCatalogRepair()
                }

                ActionButton {
                    objectName: "catalogRepairConfirmButton"
                    visible: appShell.catalogRepairConfirmationPending
                    text: "Confirm repair"
                    compact: true
                    Accessible.name: "Confirm catalog repair from the latest backup"
                    onClicked: {
                        if (appShell.confirmCatalogRepair())
                            catalogAssets.refresh()
                    }
                }

                ActionButton {
                    objectName: "catalogRepairCancelButton"
                    visible: appShell.catalogRepairConfirmationPending
                    text: "Cancel"
                    compact: true
                    Accessible.name: "Cancel catalog repair"
                    onClicked: appShell.cancelCatalogRepair()
                }

            }
        }
    }

    Component {
        id: libraryView

        StudioWorkspace {
            objectName: "libraryStudioWorkspace"
            page: "library"
            catalogAssets: window.catalogAssetModel
            importModeOpen: window.libraryWorkspaceMode === "import"
            interfaceMode: appShell.interfaceDensity === "clean" ? "compact" : appShell.interfaceDensity

            Component.onCompleted: {
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
                if (destination === "edit") {
                    window.activatePrimaryEditTool("adjust")
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

                if (window.catalogAssetModel.selectedMediaType === "timeline"
                        && window.catalogAssetModel.selectedTimelineDocumentPath.length > 0)
                    timelineDocument.loadTimeline(window.catalogAssetModel.selectedTimelineDocumentPath)

                window.switchToWorkspaceIndex(window.timelineWorkspaceIndex)
            }
            onProjectActivated: function(destination, projectName) {
                window.openProjectWorkspace(destination, projectName)
            }
            onLocalAssetActivated: function(filePath, previewSource, displayName) {
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
            objectName: "editStudioWorkspace"
            page: "edit"
            catalogAssets: window.catalogAssetModel
            editTool: window.resolvedEditTool()
            workspaceActive: window.currentWorkspace === window.editWorkspaceIndex
            interfaceMode: appShell.interfaceDensity === "clean" ? "compact" : appShell.interfaceDensity

            Component.onCompleted: window.restorePrimaryStudioPanels(this)

            onAssetActivated: function(destination) {
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
