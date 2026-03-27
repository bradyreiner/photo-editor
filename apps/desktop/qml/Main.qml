pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "workspaces"

ApplicationWindow {
    id: window
    width: 1600
    height: 980
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
    readonly property var workspaceDetails: [
        "Catalog and intake",
        "Adjust and mask",
        "Carousel composer",
        "Timeline editor",
        "Delivery pipeline"
    ]

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

    function headerActionVisible() {
        return currentWorkspace === libraryWorkspaceIndex
                || (currentWorkspace === timelineWorkspaceIndex && timelineDocument.clipCount > 0)
    }

    function headerActionText() {
        if (currentWorkspace === libraryWorkspaceIndex)
            return "Import"
        if (currentWorkspace === timelineWorkspaceIndex)
            return "Save"
        return ""
    }

    function headerActionIcon() {
        if (currentWorkspace === libraryWorkspaceIndex)
            return "plus"
        if (currentWorkspace === timelineWorkspaceIndex)
            return "save"
        return ""
    }

    function triggerHeaderAction() {
        if (currentWorkspace === libraryWorkspaceIndex) {
            beginImportWorkspace()
            return
        }

        if (currentWorkspace === timelineWorkspaceIndex)
            saveCurrentTimeline()
    }

    function activeWorkspaceItem() {
        return workspaceLoader.item ? workspaceLoader.item : null
    }

    function activeTimelineView() {
        return currentWorkspace === timelineWorkspaceIndex ? activeWorkspaceItem() : null
    }

    function activeCarouselView() {
        return currentWorkspace === carouselWorkspaceIndex ? activeWorkspaceItem() : null
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
                "editedPreviewSource": ""
            }
        }

        if (editorUsesLocalAsset && currentWorkspace === editWorkspaceIndex && localEditorFilePath.length > 0) {
            return {
                "assetId": localEditorFilePath,
                "mediaType": "photo",
                "displayLabel": currentSelectionLabel(),
                "sourcePath": localEditorFilePath,
                "editedPreviewSource": editorPreview.previewSource
            }
        }

        if (catalogAssets.selectedAssetId.length > 0 && catalogAssets.selectedFilePath.length > 0) {
            return {
                "assetId": catalogAssets.selectedAssetId,
                "mediaType": catalogAssets.selectedMediaType,
                "displayLabel": currentSelectionLabel(),
                "sourcePath": catalogAssets.selectedFilePath,
                "editedPreviewSource": editorPreview.previewSource
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
                    preset,
                    destinationFolder))
            currentWorkspace = exportWorkspaceIndex
    }

    function loadSelectedAssetIntoEditor() {
        if (catalogAssets.selectedAssetId.length === 0 || catalogAssets.selectedMediaType !== "photo")
            return

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
        enabled: window.currentWorkspace === window.editWorkspaceIndex && editorPreview.canUndo
        onTriggered: editorPreview.undo()
    }

    Action {
        id: redoEditAction
        text: "Redo"
        shortcut: StandardKey.Redo
        enabled: window.currentWorkspace === window.editWorkspaceIndex && editorPreview.canRedo
        onTriggered: editorPreview.redo()
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
        text: "Clear Finished Exports"
        enabled: exportJobs.jobCount > 0
        onTriggered: exportJobs.clearFinishedJobs()
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
            title: "&Help"
            MenuItem { action: keyboardShortcutsAction }
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
            if (window.currentWorkspace === window.editWorkspaceIndex
                    && catalogAssets.selectedMediaType === "photo")
                window.loadSelectedAssetIntoEditor()
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.alpha(theme.ink700, 0.18) }
            GradientStop { position: 0.18; color: theme.ink900 }
            GradientStop { position: 1.0; color: theme.ink950 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            radius: theme.radius12
            color: theme.alpha(theme.ink900, 0.84)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.42)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Label {
                        text: workspaceTitles[currentWorkspace]
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: workspaceSubtitles[currentWorkspace]
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                Rectangle {
                    visible: window.currentSelectionLabel().length > 0
                    radius: 6
                    color: theme.alpha(theme.ink850, 0.9)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.5)
                    implicitWidth: selectionLabel.implicitWidth + 16
                    implicitHeight: 26

                    Label {
                        id: selectionLabel
                        anchors.centerIn: parent
                        text: window.currentSelectionLabel()
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        font.weight: Font.Medium
                    }
                }

                Label {
                    text: workspaceDetails[currentWorkspace]
                    color: theme.textFaint
                    font.family: theme.fontMono
                    font.pixelSize: 10
                }

                ActionButton {
                    visible: window.headerActionVisible()
                    text: window.headerActionText()
                    iconName: window.headerActionIcon()
                    compact: true
                    tone: currentWorkspace === timelineWorkspaceIndex ? "primary" : "secondary"
                    tooltipText: currentWorkspace === timelineWorkspaceIndex ? "Save current timeline" : "Open import workspace"
                    onClicked: window.triggerHeaderAction()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Rectangle {
                Layout.preferredWidth: 122
                Layout.fillHeight: true
                radius: theme.radius12
                color: theme.ink850
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                text: "PhotoEditor"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            Label {
                                text: workspaceTitles[currentWorkspace]
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 9
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: theme.alpha(theme.lineSoft, 0.72)
                    }

                    Repeater {
                        model: workspaceTitles

                        delegate: SidebarButton {
                            required property int index
                            required property string modelData

                            Layout.fillWidth: true
                            title: modelData
                            tooltipText: modelData + "  •  " + workspaceSubtitles[index]
                            badgeText: ""
                            badgeEmphasis: false
                            compact: false
                            kind: window.workspaceKinds[index]
                            accent: window.workspaceAccent(index)
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
                radius: theme.radius12
                color: theme.ink900
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.34)

                Loader {
                    id: workspaceLoader
                    anchors.fill: parent
                    anchors.margins: 3
                    sourceComponent: [libraryView, editView, carouselView, timelineView, exportView][window.currentWorkspace]
                }
            }
        }
    }

    AppPreferencesDialog {
        id: preferencesDialog
        parent: Overlay.overlay
    }

    KeyboardShortcutsDialog {
        id: keyboardShortcutsDialog
        parent: Overlay.overlay
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
