pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
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
    title: "Virel Studio"
    color: theme.ink950
    font.family: theme.fontUi
    font.pixelSize: 12

    Theme { id: theme }

    property bool workspaceInitialized: false
    property int currentWorkspace: 0
    property string libraryWorkspaceMode: "catalog"
    property string editWorkspaceMode: "adjust"
    property bool editorUsesLocalAsset: false
    property string localEditorFilePath: ""
    property string localEditorPreviewSource: ""
    property string localEditorDisplayName: ""
    readonly property var catalogAssetModel: catalogAssets
    readonly property bool macStyle: Qt.platform.os === "osx"
    readonly property int libraryWorkspaceIndex: 0
    readonly property int editWorkspaceIndex: 1
    readonly property int carouselWorkspaceIndex: 2
    readonly property int timelineWorkspaceIndex: 3
    readonly property int exportWorkspaceIndex: 4
    readonly property var workspaceTitles: ["Library", "Edit", "Carousel", "Timeline", "Export"]
    readonly property var workspaceKinds: ["library", "edit", "carousel", "timeline", "export"]
    readonly property var workspaceSubtitles: [
        "Browse imported assets or bring media into the catalog.",
        "Refine tone, color, crop, retouch, and local adjustments.",
        "Assemble social slides and mixed-media carousel layouts.",
        "Cut video on a dedicated editing timeline.",
        "Prepare deliveries and export jobs."
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
    readonly property bool previewUpdateVisible: editorPreview.processing
                                               && primaryActivityKind === "preview_render"
    readonly property string primaryFailureKind: {
        if (!jobQueue)
            return "none"
        jobQueue.jobCount
        jobQueue.status
        if (currentWorkspace === timelineWorkspaceIndex && jobQueue.failedJobCountForKind("timeline_render") > 0)
            return "timeline_render"
        if (currentWorkspace === exportWorkspaceIndex && jobQueue.failedJobCountForKind("asset_export") > 0)
            return "asset_export"
        if (currentWorkspace === libraryWorkspaceIndex && jobQueue.failedJobCountForKind("catalog_import") > 0)
            return "catalog_import"
        if (jobQueue.failedJobCountForKind("asset_export") > 0)
            return "asset_export"
        if (jobQueue.failedJobCountForKind("timeline_render") > 0)
            return "timeline_render"
        if (jobQueue.failedJobCountForKind("catalog_import") > 0)
            return "catalog_import"
        return "none"
    }
    readonly property var latestFailureJob: jobQueue && primaryFailureKind !== "none"
                                            ? (jobQueue.jobCount, jobQueue.status, jobQueue.latestFailedJobForKind(primaryFailureKind))
                                            : ({})
    readonly property bool primaryFailureRetryable: primaryFailureKind === "asset_export"
                                                 || primaryFailureKind === "timeline_render"
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
    readonly property int activityIndicatorBottomMargin: currentWorkspace === editWorkspaceIndex ? 104 : 12
    readonly property int activityIndicatorRightMargin: {
        const workspace = workspaceLoader.item
        if (currentWorkspace === editWorkspaceIndex &&
                workspace &&
                workspace.editingChromeVisible &&
                workspace.editInspectorPanelVisible) {
            return Math.max(12, Number(workspace.editInspectorPanelWidth || 0) + 24)
        }
        return 12
    }

    function primaryShortcut(key) {
        return (macStyle ? "Meta+" : "Ctrl+") + key
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
        const kind = workspaceKinds[index]
        if (kind === "library")
            return theme.accentCool400
        if (kind === "edit")
            return theme.accentWarm300
        if (kind === "carousel")
            return theme.success
        if (kind === "timeline")
            return theme.warning
        if (kind === "export")
            return theme.accentCool300
        return theme.textMuted
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

    function workspaceBadgeCount(index) {
        if (!jobQueue)
            return 0

        if (index === libraryWorkspaceIndex)
            return jobQueue.activeJobCountForKind("catalog_import") + jobQueue.failedJobCountForKind("catalog_import")

        if (index === timelineWorkspaceIndex)
            return jobQueue.activeJobCountForKind("timeline_render") + jobQueue.failedJobCountForKind("timeline_render")

        if (index === exportWorkspaceIndex)
            return jobQueue.activeJobCount + jobQueue.failedJobCount

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
            return jobQueue.failedJobCountForKind("catalog_import") > 0

        if (index === timelineWorkspaceIndex)
            return jobQueue.failedJobCountForKind("timeline_render") > 0

        if (index === exportWorkspaceIndex)
            return jobQueue.hasFailures

        return false
    }

    function activeWorkspaceItem() {
        return workspaceLoader.item ? workspaceLoader.item : null
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
            currentWorkspace = exportWorkspaceIndex
            return true
        }
        if (cleanTool.length === 0)
            return false
        editWorkspaceMode = cleanTool
        currentWorkspace = editWorkspaceIndex
        const workspace = activeWorkspaceItem()
        if (workspace && typeof workspace.activateEditTool === "function")
            workspace.activateEditTool(cleanTool)
        return true
    }

    function switchToWorkspaceIndex(index) {
        const workspaceIndex = Number(index)
        if (workspaceIndex < 0 || workspaceIndex >= workspaceTitles.length)
            return false

        currentWorkspace = workspaceIndex
        return true
    }

    function switchWorkspaceByNumber(number) {
        return switchToWorkspaceIndex(Number(number) - 1)
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
                "recipeJson": ""
            }
        }

        if (editorUsesLocalAsset && currentWorkspace === editWorkspaceIndex && localEditorFilePath.length > 0) {
            return {
                "assetId": localEditorFilePath,
                "mediaType": "photo",
                "displayLabel": currentSelectionLabel(),
                "sourcePath": localEditorFilePath,
                "editedPreviewSource": editorPreview.previewSource,
                "recipeJson": editorPreview.currentRecipeJson()
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
                "recipeJson": editorPreview.currentRecipeJson()
            }
        }

        if (catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedFilePath.length > 0) {
            return {
                "assetId": catalogAssets.selectedAssetId,
                "mediaType": catalogAssets.selectedMediaType,
                "displayLabel": currentSelectionLabel(),
                "sourcePath": catalogAssets.selectedFilePath,
                "editedPreviewSource": editorPreview.previewSource,
                "recipeJson": catalogAssets.selectedRecipeJson
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
                    payload.recipeJson,
                    preset,
                    destinationFolder))
            currentWorkspace = exportWorkspaceIndex
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
            catalogAssets.selectedRecipeJson
        )
    }

    function loadLocalAssetIntoEditor() {
        if (localEditorFilePath.length === 0)
            return

        editorPreview.loadAsset(
            localEditorFilePath,
            localEditorFilePath,
            localEditorPreviewSource,
            localEditorDisplayName,
            "",
            "",
            ""
        )
    }

    function syncEditorRecipeToCatalog(nextAssetId) {
        if (editorUsesLocalAsset)
            return

        const currentAssetId = editorPreview.assetId
        if (currentAssetId.length === 0)
            return

        if (nextAssetId !== undefined && nextAssetId.length > 0 && nextAssetId === currentAssetId)
            return

        catalogAssets.applyRecipeJsonToAssetIds(
                    [currentAssetId],
                    editorPreview.currentRecipeJson(),
                    ["raw", "tone", "color", "crop", "masks", "detail"])
    }

    function beginImportWorkspace() {
        libraryWorkspaceMode = "import"
        currentWorkspace = libraryWorkspaceIndex
        catalogAssets.refreshImportSources()
    }

    function openSelectedPhotoInEdit() {
        if (!canOpenSelectedPhotoInEdit())
            return

        if (catalogAssets.selectedAssetId.length === 0 && catalogAssets.assetCount > 0)
            catalogAssets.selectIndex(0)

        editorUsesLocalAsset = false
        editWorkspaceMode = "adjust"
        currentWorkspace = editWorkspaceIndex
    }

    function openSelectedTimelineDocument() {
        if (!canOpenSelectedTimeline())
            return

        timelineDocument.loadTimeline(catalogAssets.selectedTimelineDocumentPath)
        currentWorkspace = timelineWorkspaceIndex
    }

    function createTimelineFromMenu() {
        timelineDocument.createTimeline()
        currentWorkspace = timelineWorkspaceIndex
    }

    function createCarouselFromMenu() {
        carouselDocument.createCarouselWithSettings({
            "carouselName": "Untitled Carousel",
            "format": "4x5",
            "slideCount": 1
        })
        currentWorkspace = carouselWorkspaceIndex
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
            editWorkspaceMode = "adjust"
            if (catalogAssets.selectedMediaType === "photo")
                loadSelectedAssetIntoEditor()
            else
                editorPreview.loadAsset("", "", "", "", "", "", "")
            currentWorkspace = editWorkspaceIndex
            return
        }

        currentWorkspace = timelineWorkspaceIndex
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
        text: "Import Folder..."
        shortcut: StandardKey.Open
        enabled: !catalogAssets.busy
        onTriggered: {
            window.currentWorkspace = window.libraryWorkspaceIndex
            catalogAssets.importFolder()
        }
    }

    Action {
        id: refreshLibraryAction
        text: "Refresh Library"
        shortcut: window.primaryShortcut("R")
        enabled: !catalogAssets.busy
        onTriggered: {
            window.currentWorkspace = window.libraryWorkspaceIndex
            catalogAssets.refresh()
        }
    }

    Action {
        id: queueExportAction
        text: "Queue Export..."
        shortcut: window.primaryShiftShortcut("E")
        enabled: window.canQueueExportFromMenu()
        onTriggered: window.queueExportFromMenu()
    }

    Action {
        id: preferencesAction
        text: "Preferences..."
        shortcut: StandardKey.Preferences
        onTriggered: preferencesDialog.openAt(0)
    }

    Action {
        id: undoEditAction
        text: "Undo"
        shortcut: window.primaryShortcut("Z")
        enabled: (window.currentWorkspace === window.editWorkspaceIndex && editorPreview.canUndo)
                 || (window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.canUndo)
        onTriggered: {
            if (window.currentWorkspace === window.timelineWorkspaceIndex)
                timelineDocument.undo()
            else
                editorPreview.undo()
        }
    }

    Action {
        id: redoEditAction
        text: "Redo"
        shortcut: window.primaryShiftShortcut("Z")
        enabled: (window.currentWorkspace === window.editWorkspaceIndex && editorPreview.canRedo)
                 || (window.currentWorkspace === window.timelineWorkspaceIndex && timelineDocument.canRedo)
        onTriggered: {
            if (window.currentWorkspace === window.timelineWorkspaceIndex)
                timelineDocument.redo()
            else
                editorPreview.redo()
        }
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
            window.editWorkspaceMode = nextCompareMode ? "compare" : "adjust"
            window.currentWorkspace = window.editWorkspaceIndex
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
        shortcut: window.primaryAltShortcut("A")
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("adjust")
    }

    Action {
        id: cropToolAction
        text: "Crop Tool"
        shortcut: window.primaryAltShortcut("C")
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("crop")
    }

    Action {
        id: masksToolAction
        text: "Masks Tool"
        shortcut: window.primaryAltShortcut("M")
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("masks")
    }

    Action {
        id: brushToolAction
        text: "Brush Tool"
        shortcut: window.primaryAltShortcut("B")
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("brush")
    }

    Action {
        id: compareToolAction
        text: "Compare Tool"
        shortcut: window.primaryAltShortcut("P")
        enabled: editorPreview.hasAsset
        onTriggered: window.activatePrimaryEditTool("compare")
    }

    Action {
        id: exportToolAction
        text: "Export Tool"
        shortcut: window.primaryAltShortcut("E")
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
        enabled: jobQueue.jobCount > 0
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
        text: "Export Carousel..."
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
        shortcut: window.primaryShortcut("1")
        checkable: true
        checked: window.currentWorkspace === window.libraryWorkspaceIndex
        onTriggered: window.switchToWorkspaceIndex(window.libraryWorkspaceIndex)
    }

    Action {
        id: switchToEditAction
        text: "Edit"
        shortcut: window.primaryShortcut("2")
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
        onTriggered: keyboardShortcutsDialog.open()
    }

    Action {
        id: checkForUpdatesAction
        text: "Check for Updates..."
        enabled: window.macStyle && macosUpdater && macosUpdater.available
        onTriggered: macosUpdater.checkForUpdates()
    }

    Action {
        id: toggleDarkModeAction
        text: appShell.darkMode ? "Switch to Light Theme" : "Switch to Dark Theme"
        onTriggered: appShell.darkMode = !appShell.darkMode
    }

    Action {
        id: commandPaletteAction
        text: "Command Palette"
        shortcut: window.primaryShortcut("K")
        onTriggered: commandPaletteDialog.open()
    }

    Action {
        id: aboutAction
        text: "About Virel Studio"
        onTriggered: aboutDialog.open()
    }

    Action {
        id: runtimePathsAction
        text: "Show Runtime Paths"
        onTriggered: preferencesDialog.openAt(2)
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
            MenuItem { action: toggleDarkModeAction }
            MenuItem { action: commandPaletteAction }
        }

        Menu {
            title: "&Help"
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
        currentWorkspace = appShell.preferredStartupWorkspace
        workspaceInitialized = true
        appShell.currentWorkspace = currentWorkspace
    }

    onCurrentWorkspaceChanged: {
        if (workspaceInitialized)
            appShell.currentWorkspace = currentWorkspace

        if (currentWorkspace !== editWorkspaceIndex)
            syncEditorRecipeToCatalog()

        if (currentWorkspace === editWorkspaceIndex) {
            if (editorUsesLocalAsset)
                loadLocalAssetIntoEditor()
            else
                loadSelectedAssetIntoEditor()
        }
    }

    Connections {
        target: catalogAssets

        function onSelectedAssetChanged() {
            window.editorUsesLocalAsset = false
            if (window.currentWorkspace === window.editWorkspaceIndex)
                window.loadSelectedAssetIntoEditor()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: theme.ink950
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: theme.alpha(theme.ink900, 0.94)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.alpha(theme.lineSoft, 0.78)
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                spacing: 18

                BrandWordmark {
                    Layout.preferredWidth: 198
                    Layout.preferredHeight: implicitHeight
                    Layout.alignment: Qt.AlignVCenter
                    compact: true
                    showMark: false
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
                            Layout.preferredWidth: Math.max(74, tabLabel.implicitWidth + 18)
                            Layout.preferredHeight: 52
                            flat: true
                            hoverEnabled: true
                            focusPolicy: Qt.TabFocus
                            activeFocusOnTab: true
                            ToolTip.visible: hovered
                            ToolTip.delay: 350
                            ToolTip.timeout: 2800
                            ToolTip.text: workspaceSubtitles[index]
                            onClicked: window.currentWorkspace = index

                            background: Item {
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 2
                                    color: workspaceTab.index === window.currentWorkspace
                                           ? window.workspaceAccent(workspaceTab.index)
                                           : "transparent"
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.topMargin: 7
                                    anchors.bottomMargin: 7
                                    color: workspaceTab.down
                                           ? theme.alpha(theme.polar, 0.08)
                                           : workspaceTab.hovered
                                             ? theme.alpha(theme.polar, 0.045)
                                             : "transparent"
                                }
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
                        elide: Text.ElideMiddle
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
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                sourceComponent: [libraryView, editView, carouselView, timelineView, exportView][window.currentWorkspace]
            }

            InlineActivityIndicator {
                id: floatingActivityIndicator
                objectName: "floatingActivityIndicator"
                visible: window.blockingActivityVisible
                width: Math.min(320, Math.max(190, parent.width * 0.26))
                height: implicitHeight
                z: 2
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: window.activityIndicatorRightMargin
                anchors.bottomMargin: window.activityIndicatorBottomMargin
                title: window.primaryActivityTitle
                detail: window.primaryActivityDetail
                progressValue: window.primaryActivityProgress
                accent: window.primaryActivityAccent
                cancelable: window.primaryActivityCancelable
                onCancelRequested: window.cancelPrimaryActivity()
            }

            InlineActivityIndicator {
                id: failureActivityIndicator
                objectName: "failureActivityIndicator"
                visible: !window.blockingActivityVisible && window.primaryFailureKind !== "none"
                width: Math.min(340, Math.max(210, parent.width * 0.28))
                height: implicitHeight
                z: 2
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: window.activityIndicatorRightMargin
                anchors.bottomMargin: window.activityIndicatorBottomMargin
                title: window.primaryFailureTitle
                detail: window.primaryFailureDetail
                progressValue: 0
                accent: theme.error
                retryable: window.primaryFailureRetryable
                onRetryRequested: window.retryPrimaryFailure()
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
                    text: "Virel Studio"
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

    AppPreferencesDialog {
        id: preferencesDialog
        parent: Overlay.overlay
    }

    StartupLoadingPopup {
        id: startupLoadingPopup
        parent: Overlay.overlay
        catalogLoading: catalogAssets.loading
        catalogStatus: catalogAssets.status
    }

    KeyboardShortcutsDialog {
        id: keyboardShortcutsDialog
        objectName: "keyboardShortcutsDialog"
        parent: Overlay.overlay
    }

    CommandPaletteDialog {
        id: commandPaletteDialog
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
            "Queue Export...",
            "Reset Adjustments",
            "Compare View",
            "Crop Overlay",
            "Select Tool",
            "Razor Tool",
            "Slip Tool",
            "Roll Tool",
            "Add Slide",
            "Export Carousel...",
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
            { action: toggleDarkModeAction, category: "View", shortcutHint: "" },
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

    Dialog {
        id: aboutDialog
        parent: Overlay.overlay
        modal: true
        width: 420
        standardButtons: Dialog.Ok
        title: "About Virel Studio"

        background: Rectangle {
            radius: theme.radius16
            color: theme.ink900
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.85)
        }

        contentItem: ColumnLayout {
            spacing: 12

            BrandWordmark {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
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
                    aboutDialog.close()
                    macosUpdater.checkForUpdates()
                }
            }
        }
    }

    Component {
        id: libraryView

        StudioWorkspace {
            page: "library"
            catalogAssets: window.catalogAssetModel
            importModeOpen: window.libraryWorkspaceMode === "import"
            onAssetActivated: function(destination) {
                if (destination === "edit") {
                    window.editWorkspaceMode = "adjust"
                    window.currentWorkspace = window.editWorkspaceIndex
                    return
                }

                if (destination === "masks") {
                    window.editWorkspaceMode = "masks"
                    window.currentWorkspace = window.editWorkspaceIndex
                    return
                }

                if (destination === "compare") {
                    window.editWorkspaceMode = "compare"
                    window.currentWorkspace = window.editWorkspaceIndex
                    return
                }

                if (destination === "export") {
                    window.currentWorkspace = window.exportWorkspaceIndex
                    return
                }

                if (catalogAssets.selectedMediaType === "timeline"
                        && catalogAssets.selectedTimelineDocumentPath.length > 0)
                    timelineDocument.loadTimeline(catalogAssets.selectedTimelineDocumentPath)

                window.currentWorkspace = window.timelineWorkspaceIndex
            }
            onProjectActivated: function(destination, projectName) {
                window.openProjectWorkspace(destination, projectName)
            }
            onLocalAssetActivated: function(filePath, previewSource, displayName) {
                window.editorUsesLocalAsset = true
                window.localEditorFilePath = filePath
                window.localEditorPreviewSource = previewSource
                window.localEditorDisplayName = displayName
                window.loadLocalAssetIntoEditor()
                window.editWorkspaceMode = "adjust"
                window.currentWorkspace = window.editWorkspaceIndex
            }
        }
    }

    Component {
        id: editView

        StudioWorkspace {
            page: "edit"
            catalogAssets: window.catalogAssetModel
            editTool: window.editWorkspaceMode === "compare" ? "compare"
                      : window.editWorkspaceMode === "masks" ? "masks"
                      : window.editWorkspaceMode === "crop" ? "crop"
                      : window.editWorkspaceMode === "brush" ? "brush"
                      : "adjust"
            onAssetActivated: function(destination) {
                if (destination === "library") {
                    window.currentWorkspace = window.libraryWorkspaceIndex
                    return
                }
                if (destination === "masks") {
                    window.editWorkspaceMode = "masks"
                    window.currentWorkspace = window.editWorkspaceIndex
                    return
                }
                if (destination === "compare") {
                    window.editWorkspaceMode = "compare"
                    window.currentWorkspace = window.editWorkspaceIndex
                    return
                }
                if (destination === "export") {
                    window.currentWorkspace = window.exportWorkspaceIndex
                    return
                }
            }
        }
    }

    Component {
        id: carouselView

        CarouselView {}
    }

    Component {
        id: timelineView

        TimelineView {}
    }

    Component {
        id: exportView

        StudioWorkspace {
            page: "export"
            catalogAssets: window.catalogAssetModel
        }
    }
}
