import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.folderlistmodel
import "../components"

Rectangle {
    id: root
    color: "transparent"
    focus: true

    Theme {
        id: theme
    }

    component InspectorRule: Rectangle {
        Theme { id: ruleTheme }
        Layout.fillWidth: true
        implicitHeight: 1
        color: ruleTheme.alpha(ruleTheme.lineSoft, 0.16)
    }

    component InspectorDetailRow: RowLayout {
        id: detailRow
        property string label: ""
        property string value: ""
        property bool showWhen: value.length > 0

        Theme { id: rowTheme }

        Layout.fillWidth: true
        visible: showWhen
        spacing: rowTheme.space10

        Label {
            Layout.preferredWidth: 74
            text: detailRow.label
            color: rowTheme.textFaint
            font.family: rowTheme.fontMono
            font.pixelSize: 10
            font.letterSpacing: 0.4
            elide: Text.ElideRight
        }

        Label {
            Layout.fillWidth: true
            text: detailRow.value
            color: rowTheme.textMain
            font.family: rowTheme.fontUi
            font.pixelSize: 12
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
    }

    signal assetActivated(string destination)
    signal projectActivated(string destination, string projectName)
    signal localAssetActivated(string filePath, string previewSource, string displayName)

    property string projectTarget: ""
    property bool importBrowserOpen: false
    property string localFolderUrl: ""
    property string localFolderPath: ""
    property string localSelectedFilePath: ""
    property string exportDestinationPath: ""
    property string catalogViewMode: "grid"
    property bool selectionModeActive: false
    property bool focusCheckEnabled: false
    readonly property var activeImportJob: {
        if (!jobQueue)
            return ({})
        jobQueue.jobCount
        jobQueue.status
        return jobQueue.latestJobForKind("catalog_import")
    }
    readonly property bool hasSelection: catalogAssets.selectedAssetId.length > 0
    readonly property bool libraryActivityVisible: catalogAssets.loading
                                                || catalogAssets.deleting
                                                || hasActiveImportJobs()
    readonly property var selectionMetrics: {
        catalogAssets.selectedAssetIds
        catalogAssets.assetCount
        let metrics = {
            count: Number(catalogAssets.selectedAssetCount || 0),
            photos: 0,
            videos: 0,
            timelines: 0,
            online: 0,
            offline: 0
        }
        for (let index = 0; index < catalogAssets.assetCount; ++index) {
            const asset = catalogAssets.assetAt(index)
            const assetId = String(asset.assetId || "")
            if (assetId.length === 0 || !catalogAssets.assetSelected(assetId))
                continue

            const mediaType = String(asset.mediaType || "photo")
            if (mediaType === "timeline")
                metrics.timelines += 1
            else if (mediaType === "video")
                metrics.videos += 1
            else
                metrics.photos += 1

            if (Boolean(asset.online))
                metrics.online += 1
            else
                metrics.offline += 1
        }
        return metrics
    }
    readonly property bool filtersActive: catalogAssets.searchFilter !== ""
                                         || catalogAssets.importDateFilter !== "all"
                                         || catalogAssets.captureYearFilter !== "all"
                                         || catalogAssets.projectFilter !== "all"
                                         || catalogAssets.mediaTypeFilter !== "all"
                                         || catalogAssets.reviewFilter !== "all"
                                         || catalogAssets.availabilityFilter !== "all"
                                         || catalogAssets.minimumRatingFilter > 0

    readonly property var importFilterValues: ["all", "today", "7d", "30d", "year"]
    readonly property var importFilterLabels: ["All Imports", "Today", "Last 7 Days", "Recent", "This Year"]
    readonly property var mediaFilterValues: ["all", "photos", "videos", "timelines"]
    readonly property var mediaFilterLabels: ["All Media", "Photos", "Videos", "Timelines"]
    readonly property var reviewFilterValues: ["all", "picked", "rejected", "unmarked", "rated"]
    readonly property var reviewFilterLabels: ["All Review States", "Picked", "Rejected", "Unmarked", "Rated"]
    readonly property var availabilityFilterValues: ["all", "online", "offline"]
    readonly property var availabilityFilterLabels: ["All Availability", "Online", "Offline"]
    readonly property var minimumRatingValues: [0, 1, 2, 3, 4, 5]
    readonly property var minimumRatingLabels: ["Any Rating", "1+ Stars", "2+ Stars", "3+ Stars", "4+ Stars", "5 Stars"]
    readonly property var captureFilterValues: {
        const values = ["all", "unknown"];
        for (let i = 0; i < catalogAssets.captureYears.length; ++i)
            values.push(catalogAssets.captureYears[i]);
        return values;
    }
    readonly property var captureFilterLabels: {
        const labels = ["All Capture Dates", "Unknown"];
        for (let i = 0; i < catalogAssets.captureYears.length; ++i)
            labels.push(catalogAssets.captureYears[i]);
        return labels;
    }
    readonly property var projectFilterValues: {
        const values = ["all", "unassigned"];
        for (let i = 0; i < catalogAssets.projectNames.length; ++i)
            values.push(catalogAssets.projectNames[i]);
        return values;
    }
    readonly property var projectFilterLabels: {
        const labels = ["All Projects", "Unassigned"];
        for (let i = 0; i < catalogAssets.projectNames.length; ++i)
            labels.push(catalogAssets.projectNames[i]);
        return labels;
    }

    function indexForValue(values, value) {
        const index = values.indexOf(value);
        return index >= 0 ? index : 0;
    }

    function fileNameForPath(filePath) {
        const parts = filePath.split("/");
        return parts.length > 0 ? parts[parts.length - 1] : filePath;
    }

    function baseNameForPath(filePath) {
        const fileName = fileNameForPath(filePath)
        const dotIndex = fileName.lastIndexOf(".")
        return dotIndex > 0 ? fileName.slice(0, dotIndex) : fileName
    }

    function activeFilterSummary() {
        const parts = [];
        if (catalogAssets.searchFilter !== "")
            parts.push("\"" + catalogAssets.searchFilter + "\"");
        if (catalogAssets.importDateFilter !== "all")
            parts.push(importFilterLabels[indexForValue(importFilterValues, catalogAssets.importDateFilter)]);
        if (catalogAssets.captureYearFilter !== "all")
            parts.push(captureFilterLabels[indexForValue(captureFilterValues, catalogAssets.captureYearFilter)]);
        if (catalogAssets.projectFilter !== "all")
            parts.push(projectFilterLabels[indexForValue(projectFilterValues, catalogAssets.projectFilter)]);
        if (catalogAssets.mediaTypeFilter !== "all")
            parts.push(mediaFilterLabels[indexForValue(mediaFilterValues, catalogAssets.mediaTypeFilter)]);
        if (catalogAssets.reviewFilter !== "all")
            parts.push(reviewFilterLabels[indexForValue(reviewFilterValues, catalogAssets.reviewFilter)]);
        if (catalogAssets.availabilityFilter !== "all")
            parts.push(availabilityFilterLabels[indexForValue(availabilityFilterValues, catalogAssets.availabilityFilter)]);
        if (catalogAssets.minimumRatingFilter > 0)
            parts.push(minimumRatingLabels[indexForValue(minimumRatingValues, catalogAssets.minimumRatingFilter)]);
        return parts.length > 0 ? parts.join("  •  ") : "Showing all imported assets";
    }

    function selectionSummaryText() {
        const metrics = root.selectionMetrics
        if (metrics.count <= 0)
            return "Select images to review, sort, export, or organize in projects."

        const parts = []
        if (metrics.photos > 0)
            parts.push(metrics.photos + (metrics.photos === 1 ? " photo" : " photos"))
        if (metrics.videos > 0)
            parts.push(metrics.videos + (metrics.videos === 1 ? " video" : " videos"))
        if (metrics.timelines > 0)
            parts.push(metrics.timelines + (metrics.timelines === 1 ? " timeline" : " timelines"))
        if (metrics.online > 0)
            parts.push(metrics.online + " online")
        if (metrics.offline > 0)
            parts.push(metrics.offline + " offline")
        return parts.join("  •  ")
    }

    function selectionToolbarText() {
        if (catalogAssets.selectedAssetCount > 0)
            return (catalogAssets.selectedAssetCount === 1
                    ? "1 item selected"
                    : catalogAssets.selectedAssetCount + " items selected") + "  •  " + root.selectionSummaryText()
        if (root.selectionModeActive)
            return "Selection mode on. Click images to build a batch."
        return ""
    }

    function selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType, modifiers) {
        if (root.selectionModeActive) {
            catalogAssets.selectIndex(index)
            catalogAssets.toggleSelectionIndex(index)
        } else {
            catalogAssets.selectIndex(index)
        }
        root.syncPreviewWithSelection()
    }

    function setSelectionModeActive(active) {
        if (root.selectionModeActive === active)
            return

        root.selectionModeActive = active
        if (active) {
            root.catalogViewMode = "grid"
            return
        }

        catalogAssets.clearSelection()
    }

    function canSelectAll() {
        return !root.importBrowserOpen && !catalogAssets.busy && catalogAssets.assetCount > 0
    }

    function selectAll() {
        if (!canSelectAll())
            return
        root.selectionModeActive = true
        catalogAssets.selectAll()
        root.syncPreviewWithSelection()
    }

    function canDeselect() {
        return root.selectionModeActive || catalogAssets.selectedAssetCount > 0
    }

    function deselectAll() {
        if (!canDeselect())
            return
        root.selectionModeActive = false
        catalogAssets.clearSelection()
    }

    function chooseImportFolder() {
        const folderUrl = catalogAssets.pickFolder("Browse Media Folder");
        if (folderUrl.length === 0)
            return;
        openLocalFolder(folderUrl);
        importBrowserOpen = true;
    }

    function openLocalFolder(folderUrl) {
        if (folderUrl.length === 0)
            return;
        localFolderUrl = folderUrl;
        localFolderPath = catalogAssets.localPathForUrl(folderUrl);
        localSelectedFilePath = "";
    }

    function selectLocalFile(filePath) {
        localSelectedFilePath = filePath;
    }

    function importCurrentFolder() {
        if (localFolderPath.length === 0)
            return;
        catalogAssets.importFolderPath(localFolderPath);
        importBrowserOpen = false;
    }

    function hasActiveImportJobs() {
        return jobQueue && jobQueue.activeJobCountForKind("catalog_import") > 0
    }

    function importJobStatus() {
        return jobQueue
                ? jobQueue.statusForKind("catalog_import", catalogAssets.status)
                : catalogAssets.status
    }

    function libraryActivityTitle() {
        if (catalogAssets.deleting) {
            const count = Math.max(1, Number(catalogAssets.deleteTargetCount || 0))
            return "Removing " + count + (count === 1 ? " asset" : " assets")
        }
        if (catalogAssets.loading)
            return "Refreshing gallery"
        if (hasActiveImportJobs())
            return activeImportJob.displayLabel && activeImportJob.displayLabel.length > 0
                    ? "Importing " + activeImportJob.displayLabel
                    : "Importing media"
        return ""
    }

    function libraryActivityDetail() {
        if (catalogAssets.deleting || catalogAssets.loading)
            return catalogAssets.status
        if (hasActiveImportJobs())
            return importJobStatus()
        return ""
    }

    function libraryActivityProgress() {
        if (catalogAssets.deleting && catalogAssets.deleteTargetCount > 0)
            return Math.max(0, Math.min(1, catalogAssets.deleteProgress / catalogAssets.deleteTargetCount))
        if (catalogAssets.loading)
            return -1
        if (hasActiveImportJobs()) {
            const status = activeImportJob.status ? String(activeImportJob.status) : ""
            const raw = activeImportJob.progress !== undefined ? Number(activeImportJob.progress) : 0
            if (status === "Queued")
                return 0.08
            if (raw > 0)
                return Math.max(0.05, Math.min(1, raw / 100.0))
        }
        return -1
    }

    function openJobsWorkspace() {
        const appWindow = root.Window.window
        if (appWindow)
            appWindow.currentWorkspace = appWindow.exportWorkspaceIndex
    }

    function clearFilters() {
        catalogAssets.searchFilter = "";
        catalogAssets.importDateFilter = "all";
        catalogAssets.captureYearFilter = "all";
        catalogAssets.projectFilter = "all";
        catalogAssets.mediaTypeFilter = "all";
        catalogAssets.reviewFilter = "all";
        catalogAssets.availabilityFilter = "all";
        catalogAssets.minimumRatingFilter = 0;
    }

    function cullStatusLabel(status) {
        if (status === "picked")
            return "Picked"
        if (status === "rejected")
            return "Rejected"
        return "Unmarked"
    }

    function ratingLabel(rating) {
        if (rating <= 0)
            return "Unrated"
        return rating + (rating === 1 ? " star" : " stars")
    }

    function cullSummary() {
        return ratingLabel(catalogAssets.selectedRating) + "  •  " + cullStatusLabel(catalogAssets.selectedCullStatus)
    }

    function availabilityLabel(online) {
        return online ? "Online" : "Offline"
    }

    function chooseRelinkFolder(selectedOnly) {
        const folderUrl = catalogAssets.pickFolder(selectedOnly ? "Relink Selected Asset" : "Relink Offline Assets");
        if (folderUrl.length === 0)
            return;
        if (selectedOnly)
            catalogAssets.relinkSelectedAssetToFolder(folderUrl)
        else
            catalogAssets.relinkOfflineAssetsToFolder(folderUrl)
    }

    function queueSelectedExports(destinationFolder, presetName) {
        if (catalogAssets.selectedAssetCount <= 0)
            return

        if (destinationFolder.length === 0)
            return

        const preset = presetName && presetName.length > 0 ? presetName : "Full Resolution"
        let assets = []
        for (let index = 0; index < catalogAssets.assetCount; ++index) {
            const asset = catalogAssets.assetAt(index)
            const assetId = String(asset.assetId || "")
            if (assetId.length === 0 || !catalogAssets.assetSelected(assetId))
                continue

            const sourcePath = String(asset.filePath || "")
            if (sourcePath.length === 0)
                continue

            assets.push({
                assetId: assetId,
                mediaType: String(asset.mediaType || "photo"),
                filePath: sourcePath,
                recipeJson: String(asset.recipeJson || ""),
                displayLabel: root.baseNameForPath(sourcePath)
            })
        }

        if (assets.length === 0)
            return

        const queuedCount = exportJobs.queueBatchExport(assets, preset, destinationFolder)
        if (queuedCount > 0)
            root.openJobsWorkspace()
    }

    function selectedProjectMembershipCount(projectName) {
        let membershipCount = 0
        for (let index = 0; index < catalogAssets.assetCount; ++index) {
            const asset = catalogAssets.assetAt(index)
            const assetId = String(asset.assetId || "")
            if (assetId.length === 0 || !catalogAssets.assetSelected(assetId))
                continue

            const projectNames = asset.projectNames || []
            if (projectNames.indexOf(projectName) !== -1)
                membershipCount += 1
        }
        return membershipCount
    }

    function selectedAssetsFullyInProject(projectName) {
        return catalogAssets.selectedAssetCount > 0
                && root.selectedProjectMembershipCount(projectName) === catalogAssets.selectedAssetCount
    }

    function projectMembershipSummary(projectName) {
        const membershipCount = root.selectedProjectMembershipCount(projectName)
        if (catalogAssets.selectedAssetCount <= 0)
            return "Select images to manage projects."
        if (membershipCount === 0)
            return "Not applied to this selection."
        if (membershipCount === catalogAssets.selectedAssetCount)
            return "Applied to all selected images."
        return membershipCount + " of " + catalogAssets.selectedAssetCount + " selected images"
    }

    function toggleSelectedProjectMembership(projectName) {
        if (catalogAssets.selectedAssetCount <= 0 || projectName.length === 0)
            return

        if (root.selectedAssetsFullyInProject(projectName)) {
            const selectedIds = catalogAssets.selectedAssetIds
            for (let index = 0; index < selectedIds.length; ++index)
                catalogAssets.removeAssetFromProject(selectedIds[index], projectName)
            return
        }

        catalogAssets.addSelectedAssetsToProject(projectName)
    }

    function openProjectAssignmentDialog() {
        if (catalogAssets.selectedAssetCount <= 0)
            return
        projectAssignmentDialog.open()
    }

    function createProjectForSelection() {
        const trimmedName = newProjectNameField.text.trim()
        if (trimmedName.length === 0)
            return

        catalogAssets.createProject(trimmedName)
        catalogAssets.addSelectedAssetsToProject(trimmedName)
        newProjectNameField.text = ""
    }

    function openExportDialog() {
        if (catalogAssets.selectedAssetCount <= 0)
            return

        if (root.exportDestinationPath.length === 0)
            root.exportDestinationPath = exportJobs.defaultDestinationDirectory()
        exportDestinationField.text = root.exportDestinationPath
        exportSelectionDialog.open()
    }

    function reviewShortcutsEnabled() {
        return !root.importBrowserOpen && !catalogAssets.busy && !root.selectionModeActive
    }

    function syncPreviewWithSelection() {
        if (catalogAssets.selectedAssetId.length === 0 || catalogAssets.selectedMediaType !== "photo" || !catalogAssets.selectedOnline)
            return

        editorPreview.loadAsset(
                    catalogAssets.selectedAssetId,
                    catalogAssets.selectedFilePath,
                    catalogAssets.selectedThumbnailSource,
                    catalogAssets.selectedCameraLabel,
                    catalogAssets.selectedCaptureDate,
                    catalogAssets.selectedLensModel,
                    catalogAssets.selectedRecipeJson)
    }

    function selectedAssetMap() {
        if (catalogAssets.selectedIndex < 0)
            return {}
        const asset = catalogAssets.assetAt(catalogAssets.selectedIndex)
        asset.visibleIndex = catalogAssets.selectedIndex
        return asset
    }

    function burstTextForAsset(asset) {
        if (!asset || !asset.burstSize || asset.burstSize <= 1)
            return ""
        return "B " + asset.burstPosition + "/" + asset.burstSize
    }

    function assetSubtitle(asset) {
        if (!asset)
            return ""
        let parts = []
        if (asset.captureDate && asset.captureDate.length > 0)
            parts.push(asset.captureDate)
        if (asset.lensModel && asset.lensModel.length > 0)
            parts.push(asset.lensModel)
        return parts.join("  •  ")
    }

    function compareAssetModel() {
        let items = []
        if (catalogAssets.selectedIndex < 0)
            return items

        let current = selectedAssetMap()
        if (!current.assetId)
            return items
        items.push(current)

        let compareIndex = catalogAssets.selectedIndex + 1 < catalogAssets.assetCount
                           ? catalogAssets.selectedIndex + 1
                           : catalogAssets.selectedIndex - 1
        if (compareIndex >= 0) {
            let compareAsset = catalogAssets.assetAt(compareIndex)
            compareAsset.visibleIndex = compareIndex
            items.push(compareAsset)
        }

        return items
    }

    function surveyAssetModel() {
        let items = []
        if (catalogAssets.selectedIndex < 0)
            return items

        const current = selectedAssetMap()
        if (!current.assetId)
            return items

        if (current.burstSize && current.burstSize > 1 && current.burstKey && current.burstKey.length > 0) {
            for (let index = 0; index < catalogAssets.assetCount; ++index) {
                let asset = catalogAssets.assetAt(index)
                if (asset.burstKey !== current.burstKey)
                    continue
                asset.visibleIndex = index
                items.push(asset)
                if (items.length >= 6)
                    break
            }
            return items
        }

        if (current.duplicateGroupSize && current.duplicateGroupSize > 1 && current.duplicateGroupKey && current.duplicateGroupKey.length > 0) {
            for (let index = 0; index < catalogAssets.assetCount; ++index) {
                let asset = catalogAssets.assetAt(index)
                if (asset.duplicateGroupKey !== current.duplicateGroupKey)
                    continue
                asset.visibleIndex = index
                items.push(asset)
                if (items.length >= 6)
                    break
            }
            return items
        }

        const start = Math.max(0, catalogAssets.selectedIndex - 1)
        const end = Math.min(catalogAssets.assetCount - 1, start + 3)
        for (let index = start; index <= end; ++index) {
            let asset = catalogAssets.assetAt(index)
            asset.visibleIndex = index
            items.push(asset)
        }
        return items
    }

    function advanceAfterReview() {
        if (catalogAssets.assetCount <= 1)
            return
        catalogAssets.selectRelative(1)
    }

    function applyPickReview(forceAdvance) {
        catalogAssets.toggleSelectedPick()
        if (forceAdvance || appShell.cullingAutoAdvance)
            advanceAfterReview()
    }

    function applyRejectReview(forceAdvance) {
        catalogAssets.toggleSelectedReject()
        if (forceAdvance || appShell.cullingAutoAdvance)
            advanceAfterReview()
    }

    function applyRatingReview(rating, forceAdvance) {
        catalogAssets.setSelectedRating(rating)
        if (forceAdvance || appShell.cullingAutoAdvance)
            advanceAfterReview()
    }

    function applyProjectSelection(projectValue) {
        catalogAssets.projectFilter = projectValue;
        if (projectValue !== "all" && projectValue !== "unassigned")
            projectTarget = projectValue;
    }

    function createAndFocusProject(projectName) {
        const trimmedName = projectName.trim();
        if (trimmedName.length === 0)
            return;
        catalogAssets.createProject(trimmedName);
        root.applyProjectSelection(trimmedName);
    }

    function syncProjectTarget() {
        if (catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned" && catalogAssets.projectFilter.length > 0) {
            projectTarget = catalogAssets.projectFilter;
            return;
        }

        if (catalogAssets.projectNames.length === 0) {
            projectTarget = "";
            return;
        }

        if (projectTarget.length === 0 || catalogAssets.projectNames.indexOf(projectTarget) === -1)
            projectTarget = catalogAssets.projectNames[0];
    }

    Component.onCompleted: syncProjectTarget()

    Connections {
        target: catalogAssets

        function onProjectNamesChanged() {
            root.syncProjectTarget();
        }

        function onFiltersChanged() {
            root.syncProjectTarget();
        }

        function onImportingChanged() {
            if (catalogAssets.importing)
                root.importBrowserOpen = false;
        }

        function onSelectedIndexChanged() {
            root.syncPreviewWithSelection()
        }
    }

    Dialog {
        id: deleteAssetsDialog
        parent: Overlay.overlay
        modal: true
        width: 420
        title: catalogAssets.selectedAssetCount === 1 ? "Delete selected image?" : "Delete selected images?"

        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0

        background: Rectangle {
            radius: theme.radiusPanel
            color: theme.ink900
            border.width: 1
            border.color: theme.alpha(theme.lineStrong, 0.36)
        }

        contentItem: ColumnLayout {
            spacing: theme.space10

            Label {
                Layout.fillWidth: true
                text: catalogAssets.selectedAssetCount === 1
                      ? "This removes 1 selected image from Gallery."
                      : "This removes " + catalogAssets.selectedAssetCount + " selected images from Gallery."
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: "Source files stay on disk. You can re-import them later if needed."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        footer: Item {
            implicitHeight: deleteDialogActions.implicitHeight + theme.space12

            RowLayout {
                id: deleteDialogActions
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: theme.space8

                ActionButton {
                    text: "Cancel"
                    onClicked: deleteAssetsDialog.close()
                }

                ActionButton {
                    text: catalogAssets.selectedAssetCount === 1 ? "Delete Image" : "Delete " + catalogAssets.selectedAssetCount + " Images"
                    iconName: "delete"
                    tone: "danger"
                    onClicked: {
                        catalogAssets.deleteSelectedAssets()
                        deleteAssetsDialog.close()
                        root.setSelectionModeActive(false)
                    }
                }
            }
        }
    }

    Dialog {
        id: projectAssignmentDialog
        parent: Overlay.overlay
        modal: true
        width: 520
        title: catalogAssets.selectedAssetCount === 1 ? "Projects for 1 selected image" : "Projects for " + catalogAssets.selectedAssetCount + " selected images"

        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0

        background: Rectangle {
            radius: theme.radiusPanel
            color: theme.ink900
            border.width: 1
            border.color: theme.alpha(theme.lineStrong, 0.36)
        }

        contentItem: ColumnLayout {
            spacing: theme.space12

            Label {
                Layout.fillWidth: true
                text: "Add the current selection to as many projects as you want, or remove it from projects it already belongs to."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: theme.space8

                    Rectangle {
                        Layout.fillWidth: true
                        visible: catalogAssets.projectNames.length === 0
                        radius: theme.radius12
                        color: theme.alpha(theme.ink950, 0.5)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.2)
                        implicitHeight: emptyProjectsLabel.implicitHeight + theme.space20

                        Label {
                            id: emptyProjectsLabel
                            anchors.fill: parent
                            anchors.margins: theme.space10
                            text: "No projects yet. Create one below."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 12
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                        }
                    }

                    Repeater {
                        model: catalogAssets.projectNames

                        delegate: Rectangle {
                            required property string modelData
                            readonly property int membershipCount: root.selectedProjectMembershipCount(modelData)
                            readonly property bool fullyApplied: membershipCount === catalogAssets.selectedAssetCount && catalogAssets.selectedAssetCount > 0

                            Layout.fillWidth: true
                            radius: theme.radius12
                            color: theme.alpha(theme.ink950, fullyApplied ? 0.64 : 0.42)
                            border.width: 1
                            border.color: fullyApplied
                                          ? theme.alpha(theme.accentCool400, 0.42)
                                          : theme.alpha(theme.lineSoft, 0.24)
                            implicitHeight: projectRow.implicitHeight + theme.space14

                            RowLayout {
                                id: projectRow
                                anchors.fill: parent
                                anchors.margins: theme.space10
                                spacing: theme.space10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        text: modelData
                                        color: theme.textStrong
                                        font.family: theme.fontUi
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        text: root.projectMembershipSummary(modelData)
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }

                                ActionButton {
                                    text: fullyApplied ? "Remove" : (membershipCount > 0 ? "Add Missing" : "Add")
                                    compact: true
                                    active: fullyApplied
                                    onClicked: root.toggleSelectedProjectMembership(modelData)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: theme.radius12
                color: theme.alpha(theme.ink950, 0.4)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.22)
                implicitHeight: newProjectRow.implicitHeight + theme.space14

                RowLayout {
                    id: newProjectRow
                    anchors.fill: parent
                    anchors.margins: theme.space10
                    spacing: theme.space8

                    ToneTextField {
                        id: newProjectNameField
                        Layout.fillWidth: true
                        placeholderText: "Create a new project"
                        onAccepted: root.createProjectForSelection()
                    }

                    ActionButton {
                        text: "Create + Add"
                        compact: true
                        enabled: newProjectNameField.text.trim().length > 0
                        onClicked: root.createProjectForSelection()
                    }
                }
            }
        }

        footer: Item {
            implicitHeight: projectDialogActions.implicitHeight + theme.space12

            RowLayout {
                id: projectDialogActions
                anchors.right: parent.right
                anchors.bottom: parent.bottom

                ActionButton {
                    text: "Done"
                    onClicked: projectAssignmentDialog.close()
                }
            }
        }
    }

    Dialog {
        id: exportSelectionDialog
        parent: Overlay.overlay
        modal: true
        width: 520
        title: catalogAssets.selectedAssetCount === 1 ? "Export 1 selected image" : "Export " + catalogAssets.selectedAssetCount + " selected items"

        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0

        background: Rectangle {
            radius: theme.radiusPanel
            color: theme.ink900
            border.width: 1
            border.color: theme.alpha(theme.lineStrong, 0.36)
        }

        contentItem: ColumnLayout {
            spacing: theme.space12

            Label {
                Layout.fillWidth: true
                text: "Choose a format preset and destination. Videos and timelines always export as original copies."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space4

                Label {
                    text: "Format Preset"
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                }

                ToneComboBox {
                    id: exportDialogPresetCombo
                    Layout.fillWidth: true
                    model: ["Full Resolution", "Archive TIFF", "Web Proof", "Client Preview", "Quick Web", "Original Copy"]
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space4

                Label {
                    text: "Destination"
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    ToneTextField {
                        id: exportDestinationField
                        Layout.fillWidth: true
                        placeholderText: "Export destination"
                        onTextChanged: root.exportDestinationPath = text
                    }

                    ActionButton {
                        text: "Browse"
                        compact: true
                        onClicked: {
                            const folder = exportJobs.pickDestinationFolder("Choose Export Folder")
                            if (folder.length > 0) {
                                root.exportDestinationPath = folder
                                exportDestinationField.text = folder
                            }
                        }
                    }
                }
            }
        }

        footer: Item {
            implicitHeight: exportDialogActions.implicitHeight + theme.space12

            RowLayout {
                id: exportDialogActions
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: theme.space8

                ActionButton {
                    text: "Cancel"
                    onClicked: exportSelectionDialog.close()
                }

                ActionButton {
                    text: "Queue Export"
                    iconName: "export"
                    tone: "primary"
                    enabled: root.exportDestinationPath.trim().length > 0
                    onClicked: {
                        root.queueSelectedExports(root.exportDestinationPath.trim(), exportDialogPresetCombo.currentText)
                        exportSelectionDialog.close()
                        root.setSelectionModeActive(false)
                    }
                }
            }
        }
    }

    Shortcut {
        sequence: "Esc"
        context: Qt.WindowShortcut
        enabled: root.selectionModeActive
        onActivated: root.setSelectionModeActive(false)
    }

    Shortcut {
        sequence: "Left"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.assetCount > 0
        onActivated: catalogAssets.selectRelative(-1)
    }

    Shortcut {
        sequence: "Right"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.assetCount > 0
        onActivated: catalogAssets.selectRelative(1)
    }

    Shortcut {
        sequence: "Up"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.assetCount > galleryFlow.columns
        onActivated: catalogAssets.selectRelative(-galleryFlow.columns)
    }

    Shortcut {
        sequence: "Down"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.assetCount > galleryFlow.columns
        onActivated: catalogAssets.selectRelative(galleryFlow.columns)
    }

    Shortcut {
        sequence: "P"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyPickReview(false)
    }

    Shortcut {
        sequence: "X"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRejectReview(false)
    }

    Shortcut {
        sequence: "U"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: catalogAssets.clearSelectedCullState()
    }

    Shortcut {
        sequence: "0"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(0, false)
    }

    Shortcut {
        sequence: "1"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(1, false)
    }

    Shortcut {
        sequence: "2"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(2, false)
    }

    Shortcut {
        sequence: "3"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(3, false)
    }

    Shortcut {
        sequence: "4"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(4, false)
    }

    Shortcut {
        sequence: "5"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(5, false)
    }

    Shortcut {
        sequence: "Shift+P"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyPickReview(true)
    }

    Shortcut {
        sequence: "Shift+X"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRejectReview(true)
    }

    Shortcut {
        sequence: "Shift+0"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(0, true)
    }

    Shortcut {
        sequence: "Shift+1"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(1, true)
    }

    Shortcut {
        sequence: "Shift+2"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(2, true)
    }

    Shortcut {
        sequence: "Shift+3"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(3, true)
    }

    Shortcut {
        sequence: "Shift+4"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(4, true)
    }

    Shortcut {
        sequence: "Shift+5"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyRatingReview(5, true)
    }

    FolderListModel {
        id: localFolderModel
        folder: root.localFolderUrl
        showDirs: true
        showFiles: false
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        sortCaseSensitive: false
    }

    FolderListModel {
        id: localImageModel
        folder: root.localFolderUrl
        showDirs: false
        showFiles: true
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        sortCaseSensitive: false
        nameFilters: catalogAssets.importFileNameFilters
    }

    Component {
        id: compareView

        Rectangle {
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                spacing: 10

                Repeater {
                    model: root.compareAssetModel()

                    delegate: ReviewPreview {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        title: modelData.cameraLabel
                        subtitle: root.assetSubtitle(modelData)
                        imageSource: modelData.thumbnailSource
                        selected: catalogAssets.assetSelected(modelData.assetId)
                        focusCheckEnabled: root.focusCheckEnabled
                        photoAsset: modelData.mediaType === "photo"
                        rating: modelData.rating
                        picked: modelData.picked
                        rejected: modelData.rejected
                        burstText: root.burstTextForAsset(modelData)
                        showDoubleClickHint: true
                        onActivated: root.selectCatalogAsset(
                                         modelData.visibleIndex,
                                         modelData.assetId,
                                         modelData.filePath,
                                         modelData.thumbnailSource,
                                         modelData.cameraLabel,
                                         modelData.captureDate,
                                         modelData.lensModel,
                                         modelData.recipeJson,
                                         modelData.mediaType)
                        onDoubleActivated: root.assetActivated(modelData.mediaType === "photo" ? "edit" : "timeline")
                    }
                }
            }
        }
    }

    Component {
        id: surveyView

        Rectangle {
            color: "transparent"

            GridLayout {
                anchors.fill: parent
                columns: width > 1200 ? 3 : 2
                columnSpacing: 10
                rowSpacing: 10

                Repeater {
                    model: root.surveyAssetModel()

                    delegate: ReviewPreview {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 240
                        title: modelData.cameraLabel
                        subtitle: root.assetSubtitle(modelData)
                        imageSource: modelData.thumbnailSource
                        selected: catalogAssets.assetSelected(modelData.assetId)
                        focusCheckEnabled: root.focusCheckEnabled
                        photoAsset: modelData.mediaType === "photo"
                        rating: modelData.rating
                        picked: modelData.picked
                        rejected: modelData.rejected
                        burstText: root.burstTextForAsset(modelData)
                        onActivated: root.selectCatalogAsset(
                                         modelData.visibleIndex,
                                         modelData.assetId,
                                         modelData.filePath,
                                         modelData.thumbnailSource,
                                         modelData.cameraLabel,
                                         modelData.captureDate,
                                         modelData.lensModel,
                                         modelData.recipeJson,
                                         modelData.mediaType)
                        onDoubleActivated: root.assetActivated(modelData.mediaType === "photo" ? "edit" : "timeline")
                    }
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: theme.space12

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radiusSurface
            color: theme.ink850
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.18)

            gradient: Gradient {
                GradientStop { position: 0.0; color: theme.alpha(theme.accentCool500, 0.05) }
                GradientStop { position: 0.18; color: theme.alpha(theme.ink850, 0.98) }
                GradientStop { position: 1.0; color: theme.ink900 }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.space12
                spacing: theme.space12

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: galleryHeaderColumn.implicitHeight + (theme.space12 * 2)
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink900, 0.92)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.32)

                    gradient: Gradient {
                        GradientStop { position: 0.0; color: theme.alpha(theme.accentWarm500, 0.06) }
                        GradientStop { position: 0.24; color: theme.alpha(theme.accentCool500, 0.03) }
                        GradientStop { position: 1.0; color: theme.alpha(theme.ink900, 0.98) }
                    }

                    ColumnLayout {
                        id: galleryHeaderColumn
                        anchors.fill: parent
                        anchors.margins: theme.space10
                        spacing: theme.space6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space12

                            Label {
                                text: "Gallery"
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: 24
                                font.weight: Font.DemiBold
                            }

                            Label {
                                Layout.fillWidth: true
                                text: catalogAssets.assetCount + " visible  •  "
                                      + catalogAssets.totalAssetCount + " total  •  "
                                      + catalogAssets.driveCount + " drives"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: utilityRow.implicitWidth + (theme.space8 * 2)
                                implicitHeight: utilityRow.implicitHeight + (theme.space8 * 2)
                                radius: theme.radius12
                                color: theme.alpha(theme.ink950, 0.28)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.24)

                                RowLayout {
                                    id: utilityRow
                                    anchors.fill: parent
                                    anchors.margins: theme.space8
                                    spacing: theme.space8

                                    Rectangle {
                                        implicitWidth: viewModeSegments.implicitWidth + (theme.space4 * 2)
                                        implicitHeight: viewModeSegments.implicitHeight + (theme.space4 * 2)
                                        radius: theme.radius8
                                        color: theme.alpha(theme.ink900, 0.62)
                                        border.width: 1
                                        border.color: theme.alpha(theme.lineSoft, 0.22)

                                        RowLayout {
                                            id: viewModeSegments
                                            anchors.fill: parent
                                            anchors.margins: theme.space4
                                            spacing: theme.space4

                                            ActionButton {
                                                text: "Grid"
                                                iconName: "grid"
                                                showText: false
                                                tooltipText: "Grid view"
                                                compact: true
                                                embedded: true
                                                active: root.catalogViewMode === "grid"
                                                onClicked: root.catalogViewMode = "grid"
                                            }

                                            ActionButton {
                                                text: "Compare"
                                                iconName: "compare"
                                                showText: false
                                                tooltipText: "Compare view"
                                                compact: true
                                                embedded: true
                                                active: root.catalogViewMode === "compare"
                                                enabled: catalogAssets.assetCount > 0
                                                onClicked: root.catalogViewMode = "compare"
                                            }

                                            ActionButton {
                                                text: "Survey"
                                                iconName: "survey"
                                                showText: false
                                                tooltipText: "Survey view"
                                                compact: true
                                                embedded: true
                                                active: root.catalogViewMode === "survey"
                                                enabled: catalogAssets.assetCount > 0
                                                onClicked: root.catalogViewMode = "survey"
                                            }
                                        }
                                    }

                                    ActionButton {
                                        text: root.selectionModeActive ? "Done" : "Select"
                                        iconName: "select"
                                        compact: true
                                        active: root.selectionModeActive
                                        onClicked: root.setSelectionModeActive(!root.selectionModeActive)
                                    }

                                    ActionButton {
                                        text: catalogAssets.loading ? "Refreshing..." : "Reload"
                                        iconName: "refresh"
                                        compact: true
                                        tooltipText: catalogAssets.loading ? "Refreshing gallery" : "Reload gallery"
                                        enabled: !catalogAssets.busy
                                        onClicked: catalogAssets.refresh()
                                    }
                                }
                            }
                        }

                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: libraryFilterColumn.implicitHeight + (theme.space10 * 2)
                    radius: theme.radius12
                    color: theme.alpha(theme.ink950, 0.24)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.2)

                    ColumnLayout {
                        id: libraryFilterColumn
                        anchors.fill: parent
                        anchors.margins: theme.space8
                        spacing: theme.space6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space6

                            ToneTextField {
                                id: searchField
                                Layout.fillWidth: true
                                placeholderText: "Search by filename, camera, lens, location..."
                                text: catalogAssets.searchFilter
                                onTextChanged: catalogAssets.searchFilter = text
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 126
                                model: root.importFilterLabels
                                currentIndex: root.indexForValue(root.importFilterValues, catalogAssets.importDateFilter)
                                onActivated: catalogAssets.importDateFilter = root.importFilterValues[currentIndex]
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 126
                                model: root.mediaFilterLabels
                                currentIndex: root.indexForValue(root.mediaFilterValues, catalogAssets.mediaTypeFilter)
                                onActivated: catalogAssets.mediaTypeFilter = root.mediaFilterValues[currentIndex]
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 142
                                model: root.projectFilterLabels
                                currentIndex: root.indexForValue(root.projectFilterValues, catalogAssets.projectFilter)
                                onActivated: root.applyProjectSelection(root.projectFilterValues[currentIndex])
                            }

                            FilterChip {
                                text: "All " + catalogAssets.assetCount
                                accent: theme.accentWarm400
                                active: catalogAssets.reviewFilter === "all"
                                onClicked: catalogAssets.reviewFilter = "all"
                            }

                            FilterChip {
                                text: "Picks " + catalogAssets.visiblePickedCount
                                accent: theme.success
                                active: catalogAssets.reviewFilter === "picked"
                                onClicked: catalogAssets.reviewFilter = "picked"
                            }

                            FilterChip {
                                text: "Rejects " + catalogAssets.visibleRejectedCount
                                accent: theme.error
                                active: catalogAssets.reviewFilter === "rejected"
                                onClicked: catalogAssets.reviewFilter = "rejected"
                            }

                            FilterChip {
                                text: "Unmarked " + catalogAssets.visibleUnmarkedCount
                                accent: theme.accentCool400
                                active: catalogAssets.reviewFilter === "unmarked"
                                onClicked: catalogAssets.reviewFilter = "unmarked"
                            }

                            ActionButton {
                                text: "Clear"
                                iconName: "delete"
                                showText: false
                                tooltipText: "Clear filters"
                                compact: true
                                enabled: root.filtersActive
                                onClicked: root.clearFilters()
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: root.selectionModeActive
                    implicitHeight: selectionActionRow.implicitHeight + (theme.space8 * 2)
                    radius: theme.radius12
                    color: theme.alpha(theme.accentCool500, 0.08)
                    border.width: 1
                    border.color: theme.alpha(theme.accentCool400, 0.22)

                    RowLayout {
                        id: selectionActionRow
                        anchors.fill: parent
                        anchors.margins: theme.space8
                        spacing: theme.space8

                        Label {
                            Layout.fillWidth: true
                            text: root.selectionToolbarText()
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }

                        ActionButton {
                            text: "Add to Project"
                            compact: true
                            enabled: catalogAssets.selectedAssetCount > 0
                            onClicked: root.openProjectAssignmentDialog()
                        }

                        ActionButton {
                            text: "Export"
                            iconName: "export"
                            compact: true
                            enabled: catalogAssets.selectedAssetCount > 0
                            onClicked: root.openExportDialog()
                        }

                        ActionButton {
                            text: "Delete"
                            iconName: "delete"
                            compact: true
                            tone: "danger"
                            enabled: catalogAssets.selectedAssetCount > 0
                            onClicked: deleteAssetsDialog.open()
                        }

                        ActionButton {
                            text: "Clear"
                            compact: true
                            enabled: catalogAssets.selectedAssetCount > 0
                            onClicked: catalogAssets.clearSelection()
                        }

                        ActionButton {
                            text: "Done"
                            compact: true
                            onClicked: root.setSelectionModeActive(false)
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Flickable {
                        id: galleryFlick
                        anchors.fill: parent
                        visible: root.catalogViewMode === "grid" && !root.importBrowserOpen
                        clip: true
                        contentWidth: width
                        contentHeight: galleryFlow.height
                        boundsBehavior: Flickable.StopAtBounds

                        function updateVisibleAssetRange() {
                            if (!catalogAssets)
                                return
                            if (!visible || catalogAssets.assetCount <= 0 || galleryFlow.columns <= 0) {
                                catalogAssets.setVisibleAssetRange(-1, -1)
                                return
                            }
                            const tileHeight = Math.max(1, Math.max(168, galleryFlow.tileWidth * 0.72) + galleryFlow.spacing)
                            const firstRow = Math.max(0, Math.floor(contentY / tileHeight))
                            const visibleRows = Math.max(1, Math.ceil(height / tileHeight) + 1)
                            const first = Math.max(0, firstRow * galleryFlow.columns)
                            const last = Math.min(catalogAssets.assetCount - 1, ((firstRow + visibleRows) * galleryFlow.columns) - 1)
                            catalogAssets.setVisibleAssetRange(first, last)
                        }

                        Component.onCompleted: updateVisibleAssetRange()
                        onContentYChanged: updateVisibleAssetRange()
                        onHeightChanged: updateVisibleAssetRange()
                        onWidthChanged: updateVisibleAssetRange()
                        onVisibleChanged: updateVisibleAssetRange()

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        Flow {
                            id: galleryFlow
                            width: galleryFlick.width
                            spacing: theme.space12

                            property int columns: Math.max(2, Math.floor(width / 256))
                            property real tileWidth: Math.floor((width - ((columns - 1) * spacing)) / columns)

                            onColumnsChanged: galleryFlick.updateVisibleAssetRange()
                            onTileWidthChanged: galleryFlick.updateVisibleAssetRange()

                            Repeater {
                                model: catalogAssets

                                delegate: FocusScope {
                                    id: galleryTile
                                    required property int index
                                    required property string assetId
                                    required property string captureDate
                                    required property string cameraLabel
                                    required property string mediaType
                                    required property string durationText
                                    required property string lensModel
                                    required property string locationPath
                                    required property string filePath
                                    required property string thumbnailSource
                                    required property string recipeJson
                                    required property int rating
                                    required property bool picked
                                    required property bool rejected
                                    required property string cullStatus
                                    required property int burstSize
                                    required property int burstPosition
                                    required property int duplicateGroupSize
                                    required property bool bestShotSuggested
                                    required property int syncGroupSize
                                    required property bool online
                                    readonly property bool selected: catalogAssets.assetSelected(assetId)

                                    width: galleryFlow.tileWidth
                                    height: previewFrame.height
                                    activeFocusOnTab: true

                                    Keys.onReturnPressed: root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType)
                                    Keys.onEnterPressed: root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType)
                                    Keys.onSpacePressed: root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType, Qt.ControlModifier)

                                    Rectangle {
                                        id: previewFrame
                                        anchors.top: parent.top
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        readonly property real cardAspectRatio: 0.72
                                        height: Math.max(168, width * cardAspectRatio)
                                        radius: theme.radius12
                                        color: selected
                                                 ? theme.alpha(theme.accentCool500, 0.1)
                                               : (thumbnailMouseArea.containsMouse ? theme.alpha(theme.ink800, 0.94) : theme.alpha(theme.ink900, 0.94))
                                        clip: true
                                        border.width: selected || galleryTile.activeFocus ? 2 : 1
                                        border.color: galleryTile.activeFocus
                                                      ? theme.alpha(theme.focusRing, 0.92)
                                                      : selected
                                                        ? theme.alpha(theme.accentCool400, 0.76)
                                                      : (thumbnailMouseArea.containsMouse ? theme.alpha(theme.lineStrong, 0.4) : theme.alpha(theme.lineSoft, 0.28))

                                        Rectangle {
                                            anchors.fill: parent
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: selected ? theme.alpha(theme.accentCool500, 0.12) : theme.alpha(theme.accentCool500, 0.04) }
                                                GradientStop { position: 1.0; color: theme.alpha(theme.ink950, selected ? 0.14 : 0.18) }
                                            }
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            anchors.margins: 2
                                            radius: Math.max(1, parent.radius - 2)
                                            visible: selected
                                            color: "transparent"
                                            border.width: 1
                                            border.color: theme.alpha(theme.accentCool400, 0.36)
                                        }

                                        Image {
                                            id: previewImage
                                            anchors.fill: parent
                                            source: thumbnailSource
                                            asynchronous: true
                                            cache: true
                                            fillMode: Image.PreserveAspectCrop
                                            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                            sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                                            opacity: online ? 1.0 : 0.42
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            color: theme.alpha(theme.ink950, thumbnailMouseArea.containsMouse ? 0.06 : (selected ? 0.1 : 0.0))
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            visible: galleryTile.activeFocus
                                            color: theme.alpha(theme.focusFill, 0.14)
                                        }

                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.margins: theme.space8
                                            visible: root.selectionModeActive
                                            width: 24
                                            height: 24
                                            radius: 12
                                            color: selected ? theme.accentCool400 : theme.alpha(theme.ink950, 0.62)
                                            border.width: 1
                                            border.color: selected
                                                          ? theme.alpha(theme.accentCool300, 0.92)
                                                          : theme.alpha(theme.lineStrong, 0.42)

                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: 8
                                                height: 8
                                                radius: 4
                                                color: selected ? theme.ink950 : "transparent"
                                            }
                                        }

                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            anchors.margins: theme.space8
                                            visible: selected
                                            radius: theme.radiusChip
                                            color: theme.alpha(theme.accentCool500, 0.78)
                                            implicitWidth: selectionBadge.implicitWidth + theme.space12
                                            implicitHeight: 22

                                            Label {
                                                id: selectionBadge
                                                anchors.centerIn: parent
                                                text: "Selected"
                                                color: theme.ink950
                                                font.family: theme.fontMono
                                                font.pixelSize: 9
                                                font.weight: Font.DemiBold
                                            }
                                        }

                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.topMargin: theme.space8
                                            anchors.leftMargin: root.selectionModeActive ? theme.space8 + 32 : theme.space8
                                            visible: mediaType !== "photo"
                                            radius: theme.radiusChip
                                            color: theme.alpha(theme.ink950, 0.66)
                                            border.width: 1
                                            border.color: theme.alpha(theme.lineSoft, 0.48)
                                            implicitWidth: mediaBadge.implicitWidth + theme.space12
                                            implicitHeight: 22

                                            Label {
                                                id: mediaBadge
                                                anchors.centerIn: parent
                                                text: mediaType === "timeline" ? "Timeline" : "Video"
                                                color: theme.textStrong
                                                font.family: theme.fontMono
                                                font.pixelSize: 9
                                            }
                                        }

                                        Rectangle {
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: theme.space8
                                            visible: mediaType !== "photo" && durationText.length > 0
                                            radius: theme.radiusChip
                                            color: theme.alpha(theme.ink950, 0.68)
                                            border.width: 1
                                            border.color: theme.alpha(theme.lineSoft, 0.48)
                                            implicitWidth: durationBadge.implicitWidth + theme.space12
                                            implicitHeight: 22

                                            Label {
                                                id: durationBadge
                                                anchors.centerIn: parent
                                                text: durationText
                                                color: theme.textStrong
                                                font.family: theme.fontMono
                                                font.pixelSize: 9
                                            }
                                        }

                                        MouseArea {
                                            id: thumbnailMouseArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: function(mouse) {
                                                galleryTile.forceActiveFocus()
                                                root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType, mouse.modifiers)
                                            }
                                            onDoubleClicked: {
                                                if (root.selectionModeActive)
                                                    return
                                                galleryTile.forceActiveFocus()
                                                root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType);
                                                root.assetActivated(mediaType === "photo" ? "edit" : "timeline");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Loader {
                        anchors.fill: parent
                        visible: !root.importBrowserOpen && root.catalogViewMode !== "grid"
                        active: visible
                        sourceComponent: root.catalogViewMode === "compare" ? compareView : surveyView
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: catalogAssets.assetCount === 0 && !root.importBrowserOpen
                        radius: theme.radiusSurface
                        color: theme.alpha(theme.ink900, 0.9)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.24)

                        Column {
                            anchors.centerIn: parent
                            spacing: theme.space12

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 44
                                height: 44
                                radius: 14
                                color: theme.alpha(theme.ink800, 0.9)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.24)

                                AppIcon {
                                    anchors.centerIn: parent
                                    name: catalogAssets.totalAssetCount === 0 ? "source" : "grid"
                                    color: theme.textStrong
                                    implicitWidth: 18
                                    implicitHeight: 18
                                }
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: catalogAssets.totalAssetCount === 0 ? "No imported media yet." : "No assets match the current filters."
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: 20
                                font.weight: Font.DemiBold
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: catalogAssets.totalAssetCount === 0
                                      ? "Switch to Import to bring photos and videos in from folders, cards, and drives."
                                      : "Broaden the search or reset a few filters to reopen the working set."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 12
                            }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: root.importBrowserOpen
                        radius: theme.radiusSurface
                        color: theme.alpha(theme.ink950, 0.98)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.52)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.space18
                            spacing: theme.space14

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Label {
                                        text: "Import From Finder"
                                        color: theme.textStrong
                                        font.family: theme.fontDisplay
                                        font.pixelSize: 24
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        text: root.localFolderPath.length > 0
                                              ? root.localFolderPath
                                              : "Choose a folder, preview it, then hand it off to Import."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 12
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                }

                                ActionButton {
                                    text: "Close"
                                    onClicked: root.importBrowserOpen = false
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space8

                                ActionButton {
                                    text: root.localFolderUrl.length > 0 ? "Choose Another Folder" : "Choose Folder"
                                    tone: "primary"
                                    onClicked: root.chooseImportFolder()
                                }

                                ActionButton {
                                    text: "Up One Folder"
                                    enabled: root.localFolderUrl.length > 0
                                    onClicked: root.openLocalFolder(catalogAssets.parentFolderUrl(root.localFolderUrl))
                                }

                                ActionButton {
                                    text: root.hasActiveImportJobs() ? "Importing..." : "Import This Folder"
                                    tone: "primary"
                                    enabled: root.localFolderPath.length > 0 && !catalogAssets.busy
                                    onClicked: root.importCurrentFolder()
                                }

                                ActionButton {
                                    text: "Jobs"
                                    compact: true
                                    visible: jobQueue && jobQueue.totalJobCountForKind("catalog_import") > 0
                                    onClicked: root.openJobsWorkspace()
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.importJobStatus()
                                color: root.hasActiveImportJobs() ? theme.accentWarm300 : theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.localFolderUrl.length > 0 ? 38 : 0
                                visible: root.localFolderUrl.length > 0
                                clip: true
                                orientation: ListView.Horizontal
                                boundsBehavior: Flickable.StopAtBounds
                                spacing: theme.space8
                                model: localFolderModel

                                delegate: Rectangle {
                                    required property int index
                                    required property string fileName
                                    required property string fileUrl

                                    width: Math.max(108, folderLabel.implicitWidth + theme.space20)
                                    height: 34
                                    radius: theme.radius8
                                    color: theme.alpha(theme.ink900, 0.92)
                                    border.width: 1
                                    border.color: theme.alpha(theme.lineSoft, 0.48)

                                    Label {
                                        id: folderLabel
                                        anchors.centerIn: parent
                                        text: fileName
                                        color: theme.textMain
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        font.weight: Font.Medium
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.openLocalFolder(fileUrl)
                                    }
                                }
                            }

                            GridView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                cellWidth: 208
                                cellHeight: 210
                                boundsBehavior: Flickable.StopAtBounds
                                model: localImageModel

                                delegate: Rectangle {
                                    required property int index
                                    required property string filePath
                                    required property string fileName
                                    required property string fileUrl

                                    width: 204
                                    height: 206
                                    radius: theme.radius12
                                    color: theme.alpha(theme.ink900, 0.94)
                                    border.width: root.localSelectedFilePath === filePath ? 1 : 0
                                    border.color: theme.accentWarm400

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: theme.space8
                                        spacing: theme.space6

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 156
                                            radius: theme.radius10
                                            color: theme.ink950
                                            clip: true

                                            Image {
                                                anchors.fill: parent
                                                source: fileUrl
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                                cache: true
                                            }
                                        }

                                        Label {
                                            text: fileName
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.selectLocalFile(filePath)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 332
            Layout.fillHeight: true
            radius: 0
            color: theme.alpha(theme.ink900, 0.74)
            border.width: 0
            border.color: "transparent"

            ScrollView {
                id: inspectorScroll
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.max(1, inspectorScroll.availableWidth - theme.space28)
                    x: theme.space14
                    y: theme.space14
                    spacing: theme.space14

                    Label {
                        Layout.fillWidth: true
                        text: "Inspector"
                        color: theme.textStrong
                        font.family: theme.fontDisplay
                        font.pixelSize: 24
                        font.weight: Font.Bold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.hasSelection
                              ? (catalogAssets.hasMultiSelection
                                 ? root.selectionSummaryText()
                                 : root.fileNameForPath(catalogAssets.selectedFilePath))
                              : "Select media to inspect it, review it, and organize it."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.hasSelection && !catalogAssets.hasMultiSelection ? 254 : 152
                        visible: true
                        radius: 0
                        color: theme.alpha(theme.ink950, 0.64)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.12)
                        clip: true

                        Image {
                            anchors.fill: parent
                            anchors.margins: root.hasSelection && !catalogAssets.hasMultiSelection ? 0 : theme.space16
                            visible: root.hasSelection && !catalogAssets.hasMultiSelection
                            source: catalogAssets.selectedThumbnailSource
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectFit
                            opacity: catalogAssets.selectedOnline ? 1.0 : 0.42
                        }

                        Column {
                            anchors.centerIn: parent
                            visible: !root.hasSelection || catalogAssets.hasMultiSelection
                            spacing: theme.space6

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: !root.hasSelection ? "No Selection" : (catalogAssets.selectedAssetCount + " Selected")
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: 18
                                font.weight: Font.Bold
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 248
                                horizontalAlignment: Text.AlignHCenter
                                text: !root.hasSelection ? "Select media from the grid to inspect it." : root.selectionSummaryText()
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.hasSelection
                        spacing: theme.space8

                        RowLayout {
                            Layout.fillWidth: true
                            visible: !catalogAssets.hasMultiSelection
                            spacing: theme.space8

                            InfoPill {
                                Layout.fillWidth: true
                                label: "MEDIA"
                                value: catalogAssets.selectedMediaType === "timeline" ? "Timeline" : catalogAssets.selectedMediaType
                            }

                            InfoPill {
                                Layout.fillWidth: true
                                label: "SOURCE"
                                value: root.availabilityLabel(catalogAssets.selectedOnline)
                                accent: catalogAssets.selectedOnline ? theme.success : theme.warning
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            visible: catalogAssets.hasMultiSelection
                            spacing: theme.space8

                            InfoPill {
                                visible: root.selectionMetrics.photos > 0
                                label: "PHOTOS"
                                value: String(root.selectionMetrics.photos)
                            }

                            InfoPill {
                                visible: root.selectionMetrics.videos > 0
                                label: "VIDEOS"
                                value: String(root.selectionMetrics.videos)
                            }

                            InfoPill {
                                visible: root.selectionMetrics.timelines > 0
                                label: "TIMELINES"
                                value: String(root.selectionMetrics.timelines)
                            }

                            InfoPill {
                                label: "ONLINE"
                                value: String(root.selectionMetrics.online)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.hasSelection && !catalogAssets.hasMultiSelection
                        spacing: theme.space8

                        InspectorRule {}

                        Label {
                            Layout.fillWidth: true
                            text: "Details"
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }

                        InspectorDetailRow {
                            label: "Camera"
                            value: catalogAssets.selectedCameraLabel.length > 0 ? catalogAssets.selectedCameraLabel : "Unknown camera"
                        }

                        InspectorDetailRow {
                            label: "Captured"
                            value: catalogAssets.selectedCaptureDate.length > 0 ? catalogAssets.selectedCaptureDate : "Unknown"
                        }

                        InspectorDetailRow {
                            label: "Lens"
                            value: catalogAssets.selectedLensModel
                        }

                        InspectorDetailRow {
                            label: "Imported"
                            value: catalogAssets.selectedImportedAt
                        }

                        InspectorDetailRow {
                            label: "Projects"
                            value: catalogAssets.selectedProjectSummary
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            InfoPill {
                                Layout.fillWidth: true
                                visible: catalogAssets.selectedIso.length > 0
                                label: "ISO"
                                value: catalogAssets.selectedIso
                            }

                            InfoPill {
                                Layout.fillWidth: true
                                visible: catalogAssets.selectedShutterSpeed.length > 0
                                label: "SHUTTER"
                                value: catalogAssets.selectedShutterSpeed
                            }

                            InfoPill {
                                Layout.fillWidth: true
                                visible: catalogAssets.selectedAperture.length > 0
                                label: "APERTURE"
                                value: catalogAssets.selectedAperture
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: catalogAssets.selectedFilePath.length > 0 ? catalogAssets.selectedFilePath : ""
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            maximumLineCount: 2
                            elide: Text.ElideMiddle
                            wrapMode: Text.WrapAnywhere
                        }
                    }

                    ColumnLayout {
                        visible: root.hasSelection && !catalogAssets.hasMultiSelection
                        Layout.fillWidth: true
                        spacing: theme.space8

                        InspectorRule {}

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            Label {
                                Layout.fillWidth: true
                                text: "Review"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 13
                                font.weight: Font.Bold
                            }

                            Label {
                                text: (catalogAssets.selectedIndex + 1) + " / " + catalogAssets.assetCount
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            InfoPill {
                                Layout.fillWidth: true
                                label: "RATING"
                                value: root.ratingLabel(catalogAssets.selectedRating)
                            }

                            InfoPill {
                                Layout.fillWidth: true
                                label: "STATUS"
                                value: root.cullStatusLabel(catalogAssets.selectedCullStatus)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Pick"
                                compact: true
                                accent: theme.success
                                active: catalogAssets.selectedPicked
                                enabled: catalogAssets.selectedIndex >= 0
                                shape: "clipped"
                                onClicked: root.applyPickReview(false)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Reject"
                                compact: true
                                accent: theme.error
                                active: catalogAssets.selectedRejected
                                enabled: catalogAssets.selectedIndex >= 0
                                onClicked: root.applyRejectReview(false)
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                text: "Clear"
                                compact: true
                                enabled: catalogAssets.selectedIndex >= 0
                                onClicked: catalogAssets.clearSelectedCullState()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space6

                            Repeater {
                                model: 5

                                delegate: ActionButton {
                                    required property int index

                                    Layout.fillWidth: true
                                    compact: true
                                    text: String(index + 1)
                                    accent: theme.warning
                                    active: catalogAssets.selectedRating >= index + 1
                                    enabled: catalogAssets.selectedIndex >= 0
                                    tooltipText: (index + 1) + ((index + 1) === 1 ? " star" : " stars")
                                    onClicked: root.applyRatingReview(index + 1, false)
                                }
                            }
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            compact: true
                            text: appShell.cullingAutoAdvance ? "Auto-Advance On" : "Auto-Advance Off"
                            active: appShell.cullingAutoAdvance
                            shape: appShell.cullingAutoAdvance ? "clipped" : "standard"
                            onClicked: appShell.cullingAutoAdvance = !appShell.cullingAutoAdvance
                        }
                    }

                    ColumnLayout {
                        visible: root.hasSelection && !catalogAssets.hasMultiSelection
                        Layout.fillWidth: true
                        spacing: theme.space8

                        InspectorRule {}

                        Label {
                            Layout.fillWidth: true
                            text: catalogAssets.selectedOnline
                                  ? "Source available on disk."
                                  : "Source offline. Recheck mounted drives or relink it."
                            color: catalogAssets.selectedOnline ? theme.textMuted : theme.warning
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Recheck"
                                onClicked: catalogAssets.refreshOnlineState()
                            }

                            ActionButton {
                                Layout.fillWidth: true
                                compact: true
                                text: "Relink"
                                enabled: !catalogAssets.selectedOnline
                                shape: !catalogAssets.selectedOnline ? "clipped" : "standard"
                                onClicked: root.chooseRelinkFolder(true)
                            }
                        }
                    }

                    Item { Layout.preferredHeight: theme.space10 }
                }
            }
        }
    }
}
