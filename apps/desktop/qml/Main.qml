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
    title: "PhotoEditor"
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
        "Refine a single image and handle local masking without leaving the canvas.",
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

    function primaryShortcut(key) {
        return (macStyle ? "Meta+" : "Ctrl+") + key
    }

    function primaryShiftShortcut(key) {
        return (macStyle ? "Meta+Shift+" : "Ctrl+Shift+") + key
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
        return catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedMediaType === "photo"
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
    }

    function openSelectedPhotoInEdit() {
        if (!canOpenSelectedPhotoInEdit())
            return

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
        currentWorkspace = timelineWorkspaceIndex
        timelineDocument.closeTimeline()
    }

    function createCarouselFromMenu() {
        currentWorkspace = carouselWorkspaceIndex
        carouselDocument.closeCarousel()
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
        shortcut: StandardKey.Undo
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
        shortcut: StandardKey.Redo
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
        onTriggered: editorPreview.compareMode = !editorPreview.compareMode
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
        checkable: true
        checked: window.currentWorkspace === window.libraryWorkspaceIndex
        onTriggered: window.currentWorkspace = window.libraryWorkspaceIndex
    }

    Action {
        id: switchToEditAction
        text: "Edit"
        checkable: true
        checked: window.currentWorkspace === window.editWorkspaceIndex
        onTriggered: window.currentWorkspace = window.editWorkspaceIndex
    }

    Action {
        id: switchToCarouselAction
        text: "Carousel"
        checkable: true
        checked: window.currentWorkspace === window.carouselWorkspaceIndex
        onTriggered: window.currentWorkspace = window.carouselWorkspaceIndex
    }

    Action {
        id: switchToTimelineAction
        text: "Timeline"
        checkable: true
        checked: window.currentWorkspace === window.timelineWorkspaceIndex
        onTriggered: window.currentWorkspace = window.timelineWorkspaceIndex
    }

    Action {
        id: switchToExportAction
        text: "Export"
        checkable: true
        checked: window.currentWorkspace === window.exportWorkspaceIndex
        onTriggered: window.currentWorkspace = window.exportWorkspaceIndex
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
        text: "About PhotoEditor"
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
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Rectangle {
                Layout.preferredWidth: 56
                Layout.fillHeight: true
                radius: 0
                color: theme.alpha(theme.glassBase, 0.72)
                border.width: 0
                border.color: "transparent"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 30
                        implicitHeight: 30
                        radius: 10
                        color: theme.alpha(theme.ink950, 0.32)
                        border.width: 1
                        border.color: theme.alpha(theme.polar, 0.12)

                        AppIcon {
                            anchors.centerIn: parent
                            name: "media"
                            color: theme.textStrong
                            implicitWidth: 14
                            implicitHeight: 14
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: theme.alpha(theme.polar, 0.12)
                    }

                    Repeater {
                        model: workspaceTitles

                        delegate: SidebarButton {
                            required property int index
                            required property string modelData

                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: implicitWidth
                            Layout.preferredHeight: implicitHeight
                            title: modelData
                            tooltipText: modelData + "  •  " + workspaceSubtitles[index]
                            compact: false
                            iconOnly: true
                            kind: window.workspaceKinds[index]
                            accent: window.workspaceAccent(index)
                            badgeText: window.workspaceBadgeText(index)
                            badgeEmphasis: window.workspaceBadgeEmphasis(index)
                            highlighted: index === window.currentWorkspace
                            onClicked: window.currentWorkspace = index
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 0
                color: theme.alpha(theme.glassBase, 0.64)
                border.width: 0
                border.color: "transparent"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 6

                    Loader {
                        id: workspaceLoader
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: [libraryView, editView, carouselView, timelineView, exportView][window.currentWorkspace]
                    }
                }

                InlineActivityIndicator {
                    id: floatingActivityIndicator
                    visible: window.anyAppActivity
                    width: Math.min(320, Math.max(190, parent.width * 0.26))
                    z: 2
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 12
                    anchors.bottomMargin: 12
                    title: window.primaryActivityTitle
                    detail: window.primaryActivityDetail
                    progressValue: window.primaryActivityProgress
                    accent: window.primaryActivityAccent
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
        parent: Overlay.overlay
    }

    CommandPaletteDialog {
        id: commandPaletteDialog
        parent: Overlay.overlay
        commands: [
            { action: switchToLibraryAction, category: "Navigate", shortcutHint: "" },
            { action: switchToEditAction, category: "Navigate", shortcutHint: "" },
            { action: switchToCarouselAction, category: "Navigate", shortcutHint: "" },
            { action: switchToTimelineAction, category: "Navigate", shortcutHint: "" },
            { action: switchToExportAction, category: "Navigate", shortcutHint: "" },
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
            { action: splitAtPlayheadAction, category: "Timeline", shortcutHint: macStyle ? "⌘\\" : "Ctrl+\\" },
            { action: removeSelectedClipAction, category: "Timeline", shortcutHint: "" },
            { action: openTimelineMediaBrowserAction, category: "Timeline", shortcutHint: macStyle ? "⇧⌘M" : "Ctrl+Shift+M" },
            { action: addTimelineVideoTrackAction, category: "Timeline", shortcutHint: macStyle ? "⇧⌘T" : "Ctrl+Shift+T" },
            { action: zoomInTimelineAction, category: "Timeline", shortcutHint: macStyle ? "⌘=" : "Ctrl+=" },
            { action: zoomOutTimelineAction, category: "Timeline", shortcutHint: macStyle ? "⌘-" : "Ctrl+-" },
            { action: zoomToFitTimelineAction, category: "Timeline", shortcutHint: macStyle ? "⌘0" : "Ctrl+0" },
            { action: timelineSelectToolAction, category: "Timeline", shortcutHint: "V" },
            { action: timelineRazorToolAction, category: "Timeline", shortcutHint: "B" },
            { action: timelineSlipToolAction, category: "Timeline", shortcutHint: "Y" },
            { action: timelineRollToolAction, category: "Timeline", shortcutHint: "N" },
            { action: toggleSnappingAction, category: "Timeline", shortcutHint: "S" },
            { action: toggleRippleAction, category: "Timeline", shortcutHint: "" },
            { action: restoreTimelineLayoutAction, category: "Timeline", shortcutHint: "" },
            { action: carouselAddSlideAction, category: "Carousel", shortcutHint: macStyle ? "⇧⌘A" : "Ctrl+Shift+A" },
            { action: carouselRemoveSlideAction, category: "Carousel", shortcutHint: "" },
            { action: carouselExportAction, category: "Carousel", shortcutHint: macStyle ? "⇧⌘C" : "Ctrl+Shift+C" },
            { action: carouselFormat4x5Action, category: "Carousel", shortcutHint: "" },
            { action: carouselFormat5x4Action, category: "Carousel", shortcutHint: "" },
            { action: toggleDarkModeAction, category: "View", shortcutHint: "" },
            { action: preferencesAction, category: "App", shortcutHint: macStyle ? "⌘," : "Ctrl+," },
            { action: keyboardShortcutsAction, category: "Help", shortcutHint: "" },
            { action: checkForUpdatesAction, category: "Help", shortcutHint: "" },
            { action: aboutAction, category: "Help", shortcutHint: "" },
            { action: clearFinishedExportsAction, category: "Export", shortcutHint: "" },
            { action: minimizeWindowAction, category: "Window", shortcutHint: "" },
            { action: zoomWindowAction, category: "Window", shortcutHint: "" },
            { action: quitAction, category: "App", shortcutHint: macStyle ? "⌘Q" : "Ctrl+Q" }
        ]
    }

    Dialog {
        id: aboutDialog
        parent: Overlay.overlay
        modal: true
        width: 420
        standardButtons: Dialog.Ok
        title: "About PhotoEditor"

        background: Rectangle {
            radius: theme.radius16
            color: theme.ink900
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.85)
        }

        contentItem: ColumnLayout {
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: "PhotoEditor Desktop"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 18
                font.weight: Font.DemiBold
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

        LibraryWorkspace {
            mode: window.libraryWorkspaceMode
            onModeSelected: function(mode) {
                window.libraryWorkspaceMode = mode
            }
            onAssetActivated: function(destination) {
                if (destination === "edit") {
                    window.editWorkspaceMode = "adjust"
                    window.currentWorkspace = window.editWorkspaceIndex
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

        EditWorkspace {
            mode: window.editWorkspaceMode
            onModeSelected: function(mode) {
                window.editWorkspaceMode = mode
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

        ExportView {}
    }
}
