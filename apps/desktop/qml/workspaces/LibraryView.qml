pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.folderlistmodel
import "../components"

Rectangle {
    id: root
    objectName: "libraryWorkspace"
    color: "transparent"
    focus: true

    Theme {
        id: theme
    }

    readonly property string libraryVisibilityContract: "library-visibility:v2|always:header-collections|contextual:media-grid-inspector-carousel-documents-project-collections-selection-import-empty-state|one-expansion:review-filter-selection-actions-import-actions-project-assignment-export|hover-focus:gallery-selection-keyboard-review|diagnostic:offline-state|preserve:functionality"
    readonly property string librarySurfaceContract: "library-surfaces:v3|browse:images-and-timelines-separated|collections:carousels-groups-recent-import-sessions|filters:search-date-media-project-review-clear|inspector:responsive-preview-metadata-review-source-actions|import-browser:folder-preview-handoff|selection:batch-project-export-delete|no-elide:true|no-overlap:true"
    readonly property string libraryWorkflowPolishContract: "library-workflow-polish:v1|first-glance:grid-dominant-header-filters-inspector|close-inspection:search-filter-selection-review-inspector-import|clutter:one-primary-selection-action-clear-filters-disabled|interaction:search-filter-select-rate-pick-reject-edit-compare-export|text:no-elide-no-overlap|functionality:preserved"
    readonly property string libraryPageCompletionContract: "page-completion:v3|library|collections:images-timelines-carousels-groups-imports|content:grid-compare-survey-timeline-groups-import-sessions-empty|inspector:responsive-media-preview-metadata-review-source-batch|toolbar:collection-view-select-refresh-filters|actions:edit-compare-export-group-delete-carousel-open-session|states:default-selected-multi-filtered-import-empty-offline"
    readonly property string librarySearchBehaviorContract: "library-search:v1|draft-state:true|debounce-ms:180|active-state:field-and-chip|clear-visible-only-when-text:true|enter-applies:true|external-sync:true|no-elide:true|functionality:preserved"

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
            font.pixelSize: rowTheme.typeMicro
            font.letterSpacing: 0.4
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: rowTheme.typeMicro
            elide: Text.ElideNone
        }

        Label {
            Layout.fillWidth: true
            text: detailRow.value
            color: rowTheme.textMain
            font.family: rowTheme.fontUi
            font.pixelSize: rowTheme.typeBody
            font.weight: Font.Medium
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
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
    property string galleryCollection: "images"
    property var carouselLibraryRecords: []
    property string pendingCarouselDocumentId: ""
    property bool pendingNewCarousel: false
    property string searchDraftText: catalogAssets.searchFilter
    property bool selectionModeActive: false
    property bool focusCheckEnabled: false
    readonly property var activeImportJob: {
        if (!jobQueue)
            return ({})
        jobQueue.jobCount
        jobQueue.status
        return jobQueue.latestJobForKind("catalog_import")
    }
    readonly property var recentImportSessions: {
        if (!jobQueue)
            return []
        jobQueue.jobCount
        const sessions = jobQueue.jobsForCenter("catalog_import").slice()
        sessions.sort(function(left, right) {
            return new Date(right.createdAt || 0).getTime() - new Date(left.createdAt || 0).getTime()
        })
        return sessions.slice(0, 24)
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
                                         || catalogAssets.importSessionFromFilter !== ""
                                         || catalogAssets.importSessionToFilter !== ""
                                         || catalogAssets.captureYearFilter !== "all"
                                         || catalogAssets.cameraFilter !== "all"
                                         || catalogAssets.captureDateFromFilter !== ""
                                         || catalogAssets.captureDateToFilter !== ""
                                         || catalogAssets.projectFilter !== "all"
                                         || (root.galleryCollection === "groupAssets"
                                             ? catalogAssets.mediaTypeFilter !== "all"
                                             : catalogAssets.mediaTypeFilter !== "images")
                                         || catalogAssets.reviewFilter !== "all"
                                         || catalogAssets.availabilityFilter !== "all"
                                         || catalogAssets.minimumRatingFilter > 0
                                         || root.galleryCollection !== "images"
    readonly property bool librarySearchHasText: root.searchDraftText.trim().length > 0
    readonly property bool librarySearchActive: catalogAssets.searchFilter.trim().length > 0
    readonly property bool librarySearchPending: root.searchDraftText !== catalogAssets.searchFilter

    readonly property var importFilterValues: ["all", "today", "7d", "30d", "year"]
    readonly property var importFilterLabels: ["All Imports", "Today", "Last 7 Days", "Recent", "This Year"]
    readonly property var mediaFilterValues: root.galleryCollection === "groupAssets"
                                             ? ["all", "images", "photos", "videos", "timelines"]
                                             : ["images", "photos", "videos"]
    readonly property var mediaFilterLabels: root.galleryCollection === "groupAssets"
                                             ? ["All Media", "All Images", "Photos", "Videos", "Timelines"]
                                             : ["All Images", "Photos", "Videos"]
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
    readonly property var cameraFilterValues: {
        const values = ["all", "unknown"];
        for (let i = 0; i < catalogAssets.cameraLabels.length; ++i)
            values.push(catalogAssets.cameraLabels[i]);
        return values;
    }
    readonly property var cameraFilterLabels: {
        const labels = ["All Cameras", "Unknown Camera"];
        for (let i = 0; i < catalogAssets.cameraLabels.length; ++i)
            labels.push(catalogAssets.cameraLabels[i]);
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
    readonly property var projectCollectionNames: ["Unassigned"].concat(catalogAssets.projectNames)

    function indexForValue(values, value) {
        const index = values.indexOf(value);
        return index >= 0 ? index : 0;
    }

    function refreshCarouselLibrary() {
        if (typeof carouselDocument === "undefined" || !carouselDocument ||
                typeof carouselDocument.recentDocuments !== "function") {
            carouselLibraryRecords = []
            return
        }
        carouselLibraryRecords = carouselDocument.recentDocuments()
    }

    function showImageCollection() {
        catalogAssets.setImportSessionFilter("", "")
        catalogAssets.projectFilter = "all"
        catalogAssets.mediaTypeFilter = "images"
        root.galleryCollection = "images"
        root.catalogViewMode = "grid"
    }

    function showTimelineCollection() {
        librarySearchDebounceTimer.stop()
        root.searchDraftText = ""
        catalogAssets.clearFilters()
        catalogAssets.setImportSessionFilter("", "")
        catalogAssets.mediaTypeFilter = "timelines"
        root.galleryCollection = "timelines"
        root.catalogViewMode = "grid"
    }

    function applyGalleryMediaFilter() {
        if (root.galleryCollection === "images")
            catalogAssets.mediaTypeFilter = "images"
        else if (root.galleryCollection === "timelines")
            catalogAssets.mediaTypeFilter = "timelines"
    }

    function importSessionSummary(session) {
        const metadata = session && session.metadata ? session.metadata : ({})
        const result = metadata.importResult || ({})
        const count = Number(result.importedCount || 0)
        if (session.status === "Completed")
            return count + (count === 1 ? " media item imported" : " media items imported")
        if (session.status === "Failed")
            return "Import failed · " + String(session.errorMessage || "Open job details")
        return String(session.status || "Import session") +
               (Number(session.progress || 0) > 0 ? " · " + Number(session.progress) + "%" : "")
    }

    function openImportSession(session) {
        if (!session || session.status !== "Completed")
            return
        const metadata = session.metadata || ({})
        const result = metadata.importResult || ({})
        if (Number(result.importedCount || 0) <= 0)
            return
        librarySearchDebounceTimer.stop()
        root.searchDraftText = ""
        catalogAssets.clearFilters()
        catalogAssets.setImportSessionFilter(String(session.createdAt || ""),
                                             String(session.updatedAt || session.createdAt || ""))
        catalogAssets.mediaTypeFilter = "images"
        root.catalogViewMode = "grid"
        root.galleryCollection = "images"
    }

    function openProjectCollection(projectName) {
        const name = String(projectName || "")
        catalogAssets.projectFilter = name.length > 0 ? name : "unassigned"
        catalogAssets.setImportSessionFilter("", "")
        catalogAssets.mediaTypeFilter = "all"
        root.catalogViewMode = "grid"
        root.galleryCollection = "groupAssets"
    }

    function requestOpenCarousel(documentId) {
        const id = String(documentId || "").trim()
        if (id.length === 0)
            return false
        if (carouselDocument.documentDirty) {
            root.pendingCarouselDocumentId = id
            root.pendingNewCarousel = false
            unsavedCarouselOpenDialog.open()
            return false
        }
        if (!carouselDocument.loadDocument(id))
            return false
        const appWindow = root.Window.window
        return appWindow && appWindow.switchToWorkspaceIndex
                ? appWindow.switchToWorkspaceIndex(appWindow.carouselWorkspaceIndex)
                : true
    }

    function saveAndOpenPendingCarousel() {
        const id = root.pendingCarouselDocumentId
        const createNew = root.pendingNewCarousel
        if ((!createNew && id.length === 0) || !carouselDocument.saveDocument())
            return false
        root.pendingCarouselDocumentId = ""
        root.pendingNewCarousel = false
        unsavedCarouselOpenDialog.close()
        if (createNew) {
            root.assetActivated("new-carousel")
            return true
        }
        if (!carouselDocument.loadDocument(id))
            return false
        const appWindow = root.Window.window
        return appWindow && appWindow.switchToWorkspaceIndex
                ? appWindow.switchToWorkspaceIndex(appWindow.carouselWorkspaceIndex)
                : true
    }

    function requestNewCarousel() {
        if (carouselDocument.documentDirty) {
            root.pendingCarouselDocumentId = ""
            root.pendingNewCarousel = true
            unsavedCarouselOpenDialog.open()
            return
        }
        root.assetActivated("new-carousel")
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
        if (catalogAssets.importSessionFromFilter !== "" || catalogAssets.importSessionToFilter !== "")
            parts.push("Selected import session");
        if (catalogAssets.importDateFilter !== "all")
            parts.push(importFilterLabels[indexForValue(importFilterValues, catalogAssets.importDateFilter)]);
        if (catalogAssets.captureYearFilter !== "all")
            parts.push(captureFilterLabels[indexForValue(captureFilterValues, catalogAssets.captureYearFilter)]);
        if (catalogAssets.cameraFilter !== "all")
            parts.push(cameraFilterLabels[indexForValue(cameraFilterValues, catalogAssets.cameraFilter)]);
        if (catalogAssets.captureDateFromFilter !== "" || catalogAssets.captureDateToFilter !== "")
            parts.push("Captured " + (catalogAssets.captureDateFromFilter || "any time") +
                       " to " + (catalogAssets.captureDateToFilter || "today"));
        if (catalogAssets.projectFilter !== "all")
            parts.push(projectFilterLabels[indexForValue(projectFilterValues, catalogAssets.projectFilter)]);
        if ((root.galleryCollection === "groupAssets" && catalogAssets.mediaTypeFilter !== "all") ||
                (root.galleryCollection !== "groupAssets" && catalogAssets.mediaTypeFilter !== "images"))
            parts.push(mediaFilterLabels[indexForValue(mediaFilterValues, catalogAssets.mediaTypeFilter)]);
        if (catalogAssets.reviewFilter !== "all")
            parts.push(reviewFilterLabels[indexForValue(reviewFilterValues, catalogAssets.reviewFilter)]);
        if (catalogAssets.availabilityFilter !== "all")
            parts.push(availabilityFilterLabels[indexForValue(availabilityFilterValues, catalogAssets.availabilityFilter)]);
        if (catalogAssets.minimumRatingFilter > 0)
            parts.push(minimumRatingLabels[indexForValue(minimumRatingValues, catalogAssets.minimumRatingFilter)]);
        return parts.length > 0 ? parts.join("  •  ") : "Showing all images";
    }

    function librarySearchBehaviorSignature() {
        return "library-search:v1" +
               "|draft:" + String(root.librarySearchHasText) +
               "|active:" + String(root.librarySearchActive) +
               "|pending:" + String(root.librarySearchPending) +
               "|clear-visible:" + String(root.librarySearchHasText) +
               "|debounce:180" +
               "|text:no-elide" +
               "|preserve:catalogAssets.searchFilter"
    }

    function libraryWorkflowPolishSignature() {
        return "library-workflow-polish:v1" +
               "|search:" + (catalogAssets.searchFilter !== "" ? "active" : "idle") +
               "|filters:" + (root.filtersActive ? "active" : "clear") +
               "|selection-mode:" + String(root.selectionModeActive) +
               "|selected:" + String(catalogAssets.selectedAssetCount || 0) +
               "|view:" + root.catalogViewMode +
               "|collection:" + root.galleryCollection +
               "|inspector:" + (root.hasSelection ? (catalogAssets.hasMultiSelection ? "batch" : "single") : "empty") +
               "|import:" + String(root.importBrowserOpen) +
               "|activity:" + String(root.libraryActivityVisible) +
               "|clear-filters-enabled:" + String(root.filtersActive) +
               "|preserve:search-filter-select-rate-pick-reject-edit-compare-export-import"
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

    function selectCatalogAsset(index, modifiers) {
        catalogAssets.selectIndexWithModifiers(index, Number(modifiers || 0), root.selectionModeActive)
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

    function importActionMenuItems() {
        return [
            {
                key: "choose-folder",
                label: "Choose Folder",
                icon: "folder",
                enabled: true,
                tooltip: "Choose a folder from Finder."
            },
            {
                key: "import-folder",
                label: "Import This Folder",
                icon: "folder",
                enabled: root.localFolderPath.length > 0 && !catalogAssets.busy,
                disabledReason: root.localFolderPath.length > 0
                                ? "Import is already working."
                                : "Choose a folder first.",
                tooltip: "Import the currently previewed folder into the Library."
            },
            {
                key: "up-one-folder",
                label: "Up One Folder",
                icon: "chevron-left",
                enabled: root.localFolderUrl.length > 0,
                disabledReason: "Choose a folder before navigating.",
                tooltip: "Open the parent folder."
            },
            {
                key: "jobs",
                label: "Show Import Jobs",
                icon: "clock",
                enabled: jobQueue && jobQueue.totalJobCountForKind("catalog_import") > 0,
                disabledReason: "No import jobs are queued.",
                tooltip: "Open the background job view."
            }
        ]
    }

    function handleImportMenuAction(key) {
        if (key === "choose-folder") {
            chooseImportFolder()
        } else if (key === "import-folder") {
            importCurrentFolder()
        } else if (key === "up-one-folder") {
            openLocalFolder(catalogAssets.parentFolderUrl(localFolderUrl))
        } else if (key === "jobs") {
            openJobsWorkspace()
        }
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
        const groupFilter = root.galleryCollection === "groupAssets" ? catalogAssets.projectFilter : ""
        librarySearchDebounceTimer.stop();
        root.searchDraftText = "";
        catalogAssets.clearFilters();
        if (groupFilter.length > 0)
            catalogAssets.projectFilter = groupFilter;
        catalogAssets.mediaTypeFilter = root.galleryCollection === "groupAssets" ? "all" : "images";
    }

    function updateLibrarySearchDraft(text) {
        root.searchDraftText = String(text || "")
        librarySearchDebounceTimer.restart()
    }

    function applyLibrarySearchNow() {
        librarySearchDebounceTimer.stop()
        if (catalogAssets.searchFilter !== root.searchDraftText)
            catalogAssets.searchFilter = root.searchDraftText
    }

    function clearLibrarySearch() {
        librarySearchDebounceTimer.stop()
        root.searchDraftText = ""
        catalogAssets.searchFilter = ""
        searchField.forceActiveFocus()
    }

    Timer {
        id: librarySearchDebounceTimer
        interval: 180
        repeat: false
        onTriggered: root.applyLibrarySearchNow()
    }

    Connections {
        target: catalogAssets
        function onFiltersChanged() {
            if (!librarySearchDebounceTimer.running && root.searchDraftText !== catalogAssets.searchFilter)
                root.searchDraftText = catalogAssets.searchFilter
        }
    }

    function reviewFilterLabel() {
        if (catalogAssets.reviewFilter === "picked")
            return "Picked " + catalogAssets.visiblePickedCount
        if (catalogAssets.reviewFilter === "rejected")
            return "Rejected " + catalogAssets.visibleRejectedCount
        if (catalogAssets.reviewFilter === "unmarked")
            return "Unmarked " + catalogAssets.visibleUnmarkedCount
        if (catalogAssets.reviewFilter === "rated")
            return "Rated"
        return "Review"
    }

    function reviewFilterMenuItems() {
        return [
            {
                key: "all",
                label: "All " + catalogAssets.assetCount,
                icon: "grid",
                checked: catalogAssets.reviewFilter === "all",
                tooltip: "Show every visible asset."
            },
            {
                key: "picked",
                label: "Picked " + catalogAssets.visiblePickedCount,
                icon: "check",
                checked: catalogAssets.reviewFilter === "picked",
                tooltip: "Show picked assets."
            },
            {
                key: "rejected",
                label: "Rejected " + catalogAssets.visibleRejectedCount,
                icon: "close",
                checked: catalogAssets.reviewFilter === "rejected",
                tooltip: "Show rejected assets."
            },
            {
                key: "unmarked",
                label: "Unmarked " + catalogAssets.visibleUnmarkedCount,
                icon: "media",
                checked: catalogAssets.reviewFilter === "unmarked",
                tooltip: "Show assets without pick or reject state."
            },
            {
                key: "rated",
                label: "Rated",
                icon: "star",
                checked: catalogAssets.reviewFilter === "rated",
                tooltip: "Show rated assets."
            }
        ]
    }

    function handleReviewFilterMenu(key) {
        catalogAssets.reviewFilter = key
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
                adjustmentPatchJson: String(asset.adjustmentPatchJson || ""),
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
            catalogAssets.removeSelectedAssetsFromProject(projectName)
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

        catalogAssets.createProjectWithSelectedAssets(trimmedName)
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

    function selectionActionMenuItems() {
        return [
            {
                key: "select-all",
                label: "Select All Visible",
                icon: "select",
                enabled: root.canSelectAll(),
                tooltip: "Select every visible Library asset."
            },
            {
                key: "clear-selection",
                label: "Clear Selection",
                icon: "close",
                enabled: catalogAssets.selectedAssetCount > 0,
                disabledReason: "No assets are selected.",
                tooltip: "Clear the current batch selection."
            },
            {
                key: "done",
                label: "Done Selecting",
                icon: "check",
                enabled: root.selectionModeActive,
                disabledReason: "Selection mode is not active.",
                tooltip: "Leave selection mode."
            },
            {
                key: "delete",
                label: catalogAssets.selectedAssetCount === 1 ? "Delete Image" : "Delete Selected",
                icon: "delete",
                destructive: true,
                separatorBefore: true,
                enabled: catalogAssets.selectedAssetCount > 0,
                disabledReason: "Select one or more Library assets before deleting.",
                tooltip: "Delete selected assets from the Library."
            }
        ]
    }

    function handleSelectionActionMenu(key) {
        if (key === "select-all") {
            root.selectAll()
            return
        }
        if (key === "clear-selection") {
            catalogAssets.clearSelection()
            return
        }
        if (key === "done") {
            root.setSelectionModeActive(false)
            return
        }
        if (key === "delete" && catalogAssets.selectedAssetCount > 0)
            deleteAssetsDialog.open()
    }

    function reviewShortcutsEnabled() {
        return !root.importBrowserOpen && !catalogAssets.busy && !root.selectionModeActive
    }

    function openSelectedAsset(destination) {
        if (catalogAssets.selectedAssetId.length === 0 || catalogAssets.selectedMediaType !== "photo" || !catalogAssets.selectedOnline)
            return root.assetActivated(destination)

        editorPreview.loadAsset(
                    catalogAssets.selectedAssetId,
                    catalogAssets.selectedFilePath,
                    catalogAssets.selectedThumbnailSource,
                    catalogAssets.selectedCameraLabel,
                    catalogAssets.selectedCaptureDate,
                    catalogAssets.selectedLensModel,
                    catalogAssets.selectedAdjustmentPatchJson)
        root.assetActivated(destination)
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
        return (asset.burstTimeBased ? "Burst " : "Sequence ") + asset.burstPosition + "/" + asset.burstSize
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

    function applyColorLabelReview(colorLabel, forceAdvance) {
        catalogAssets.setSelectedColorLabel(colorLabel)
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
        root.galleryCollection = "images";
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

    Component.onCompleted: {
        syncProjectTarget()
        refreshCarouselLibrary()
        if (visible)
            root.applyGalleryMediaFilter()
    }

    onVisibleChanged: {
        if (visible) {
            root.applyGalleryMediaFilter()
            carouselRefreshTimer.restart()
        } else {
            catalogAssets.setImportSessionFilter("", "")
            if (catalogAssets.mediaTypeFilter === "images" ||
                    catalogAssets.mediaTypeFilter === "photos" ||
                    catalogAssets.mediaTypeFilter === "videos" ||
                    catalogAssets.mediaTypeFilter === "timelines")
                catalogAssets.mediaTypeFilter = "all"
        }
    }

    Timer {
        id: carouselRefreshTimer
        interval: 350
        repeat: false
        onTriggered: root.refreshCarouselLibrary()
    }

    Connections {
        target: carouselDocument
        function onDocumentStateChanged() {
            if (root.visible && !carouselDocument.documentDirty &&
                    (carouselDocument.saveState === "Saved" || carouselDocument.saveState === "Autosaved"))
                carouselRefreshTimer.restart()
        }
    }

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
            border.width: theme.hairlineWidth
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
                font.pixelSize: theme.typeBody
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: "Source files stay on disk. You can re-import them later if needed."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
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
            border.width: theme.hairlineWidth
            border.color: theme.alpha(theme.lineStrong, 0.36)
        }

        contentItem: ColumnLayout {
            spacing: theme.space12

            Label {
                Layout.fillWidth: true
                text: "Add the current selection to as many projects as you want, or remove it from projects it already belongs to."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
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
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.2)
                        implicitHeight: emptyProjectsLabel.implicitHeight + theme.space20

                        Label {
                            id: emptyProjectsLabel
                            anchors.fill: parent
                            anchors.margins: theme.space10
                            text: "No projects yet. Create one below."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeBody
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                        }
                    }

                    Repeater {
                        model: catalogAssets.projectNames

                        delegate: ProjectMembershipRow {
                            required property string modelData

                            projectName: modelData
                            summary: root.projectMembershipSummary(modelData)
                            membershipCount: root.selectedProjectMembershipCount(modelData)
                            selectedCount: catalogAssets.selectedAssetCount
                            onToggleRequested: function(projectName) { root.toggleSelectedProjectMembership(projectName) }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: theme.radius12
                color: theme.alpha(theme.ink950, 0.4)
                border.width: theme.hairlineWidth
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
        id: unsavedCarouselOpenDialog
        objectName: "libraryUnsavedCarouselOpenDialog"
        parent: Overlay.overlay
        modal: true
        width: Math.min(theme.scaled(460), parent ? parent.width - theme.space24 : theme.scaled(460))
        title: root.pendingNewCarousel ? "Save before creating a Carousel?" : "Save before opening another Carousel?"

        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0

        background: Rectangle {
            radius: theme.radiusPanel
            color: theme.surfaceTop
            border.width: theme.hairlineWidth
            border.color: theme.alpha(theme.lineStrong, 0.42)
        }

        contentItem: ColumnLayout {
            spacing: theme.space10

            Label {
                Layout.fillWidth: true
                text: root.pendingNewCarousel
                      ? "Save the open Carousel before starting a new one?"
                      : "Save the open Carousel before loading another one?"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: "Saving keeps your current edits in the Gallery."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
            }
        }

        footer: RowLayout {
            spacing: theme.space8

            Item { Layout.fillWidth: true }

            ActionButton {
                text: "Cancel"
                onClicked: {
                    root.pendingCarouselDocumentId = ""
                    root.pendingNewCarousel = false
                    unsavedCarouselOpenDialog.close()
                }
            }

            ActionButton {
                objectName: "librarySaveAndOpenCarouselButton"
                text: root.pendingNewCarousel ? "Save & Create" : "Save & Open"
                iconName: "save"
                tone: "primary"
                onClicked: root.saveAndOpenPendingCarousel()
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
            border.width: theme.hairlineWidth
            border.color: theme.alpha(theme.lineStrong, 0.36)
        }

        contentItem: ColumnLayout {
            spacing: theme.space12

            Label {
                Layout.fillWidth: true
                text: "Choose a format preset and destination. Videos and timelines always export as original copies."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space4

                Label {
                    text: "Format Preset"
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeMicro
                }

                ToneComboBox {
                    id: exportDialogPresetCombo
                    accessibleLabel: qsTr("Format preset")
                    Layout.fillWidth: true
                    model: ["Full Resolution", "Archive TIFF", "OpenEXR Half", "ACES2065-1 OpenEXR", "ACEScg OpenEXR", "HDR PQ AVIF", "HDR HLG AVIF", "Web Proof", "Client Preview", "Quick Web", "Original Copy"]
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space4

                Label {
                    text: "Destination"
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeMicro
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
                    text: "Start Export"
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
        sequence: "6"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyColorLabelReview("red", false)
    }

    Shortcut {
        sequence: "7"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyColorLabelReview("yellow", false)
    }

    Shortcut {
        sequence: "8"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyColorLabelReview("green", false)
    }

    Shortcut {
        sequence: "9"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyColorLabelReview("blue", false)
    }

    Shortcut {
        sequence: "Alt+9"
        context: Qt.WindowShortcut
        enabled: root.reviewShortcutsEnabled() && catalogAssets.selectedIndex >= 0
        onActivated: root.applyColorLabelReview("purple", false)
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
                spacing: theme.space10

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
                        onActivated: root.selectCatalogAsset(modelData.visibleIndex)
                        onDoubleActivated: root.openSelectedAsset(modelData.mediaType === "photo" ? "edit" : "timeline")
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
                        onActivated: root.selectCatalogAsset(modelData.visibleIndex)
                        onDoubleActivated: root.openSelectedAsset(modelData.mediaType === "photo" ? "edit" : "timeline")
                    }
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: theme.space12

        Rectangle {
            objectName: "libraryMainSurface"
            readonly property string surfaceContract: "library-main-surface:v1|browse-dominant:true|filters-visible:true|view-switch-visible:true|selection-contextual:true|import-handoff-contextual:true|no-elide:true|no-overlap:true"
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radiusSurface
            color: theme.ink850
            border.width: theme.hairlineWidth
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
                    objectName: "libraryHeaderBar"
                    readonly property string visibilityRole: "always-visible|title-counts-view-mode-selection-refresh"
                    Layout.fillWidth: true
                    implicitHeight: galleryHeaderColumn.implicitHeight + (theme.space12 * 2)
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink900, 0.92)
                    border.width: theme.hairlineWidth
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
                                text: root.galleryCollection === "timelines" ? "Timelines"
                                      : root.galleryCollection === "carousels" ? "Carousels"
                                      : root.galleryCollection === "projects" ? "Groups"
                                      : root.galleryCollection === "groupAssets"
                                        ? (catalogAssets.projectFilter === "unassigned" ? "Unassigned" : catalogAssets.projectFilter)
                                      : root.galleryCollection === "imports" ? "Recent Imports"
                                      : "Images"
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: theme.typePageTitle
                                font.weight: Font.DemiBold
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.galleryCollection === "carousels"
                                      ? root.carouselLibraryRecords.length + " saved carousels"
                                      : root.galleryCollection === "projects"
                                        ? root.projectCollectionNames.length +
                                          (root.projectCollectionNames.length === 1 ? " group" : " groups")
                                      : root.galleryCollection === "groupAssets"
                                        ? catalogAssets.assetCount + " items in this group"
                                      : root.galleryCollection === "imports"
                                          ? root.recentImportSessions.length +
                                            (root.recentImportSessions.length === 1 ? " recent import session" : " recent import sessions")
                                          : root.galleryCollection === "timelines"
                                            ? catalogAssets.assetCount + " saved timelines"
                                            : catalogAssets.assetCount + " visible images  •  "
                                              + catalogAssets.totalAssetCount + " total assets  •  "
                                              + catalogAssets.driveCount + " drives"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                verticalAlignment: Text.AlignVCenter
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: theme.typeMicro
                                elide: Text.ElideNone
                            }

                            Rectangle {
                                visible: root.galleryCollection === "images" || root.galleryCollection === "groupAssets"
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: utilityRow.implicitWidth + (theme.space8 * 2)
                                implicitHeight: utilityRow.implicitHeight + (theme.space8 * 2)
                                radius: theme.radius12
                                color: theme.alpha(theme.ink950, 0.28)
                                border.width: theme.hairlineWidth
                                border.color: theme.alpha(theme.lineSoft, 0.24)

                                RowLayout {
                                    id: utilityRow
                                    objectName: "libraryUtilityToolbar"
                                    readonly property string visibilityRole: "always-visible|view-mode-select-refresh"
                                    anchors.fill: parent
                                    anchors.margins: theme.space8
                                    spacing: theme.space8

                                    Rectangle {
                                        implicitWidth: viewModeSegments.implicitWidth + (theme.space4 * 2)
                                        implicitHeight: viewModeSegments.implicitHeight + (theme.space4 * 2)
                                        radius: theme.radius8
                                        color: theme.alpha(theme.ink900, 0.62)
                                        border.width: theme.hairlineWidth
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
                                        text: catalogAssets.loading ? "Refreshing" : "Reload"
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

                RowLayout {
                    objectName: "libraryCollectionNavigation"
                    readonly property string collectionNavigationContract: "gallery-collections:v2|images:photos-videos|timelines:saved-documents|carousels:saved-documents|groups:projects-unassigned|imports:recent-sessions|keyboard:true"
                    Layout.fillWidth: true
                    spacing: theme.space6

                    ActionButton {
                        objectName: "libraryImagesCollectionButton"
                        text: "Images"
                        iconName: "grid"
                        compact: true
                        active: root.galleryCollection === "images"
                        onClicked: root.showImageCollection()
                    }

                    ActionButton {
                        objectName: "libraryTimelinesCollectionButton"
                        text: "Timelines"
                        iconName: "timeline"
                        compact: true
                        active: root.galleryCollection === "timelines"
                        onClicked: root.showTimelineCollection()
                    }

                    ActionButton {
                        objectName: "libraryCarouselsCollectionButton"
                        text: "Boards"
                        iconName: "carousel"
                        compact: true
                        active: root.galleryCollection === "carousels"
                        onClicked: {
                            catalogAssets.setImportSessionFilter("", "")
                            root.galleryCollection = "carousels"
                            root.refreshCarouselLibrary()
                        }
                    }

                    ActionButton {
                        objectName: "libraryGroupsCollectionButton"
                        text: "Groups"
                        iconName: "folder"
                        compact: true
                        active: root.galleryCollection === "projects" || root.galleryCollection === "groupAssets"
                        onClicked: {
                            catalogAssets.setImportSessionFilter("", "")
                            catalogAssets.projectFilter = "all"
                            catalogAssets.mediaTypeFilter = "images"
                            root.galleryCollection = "projects"
                        }
                    }

                    ActionButton {
                        objectName: "libraryImportsCollectionButton"
                        text: "Recent Imports"
                        iconName: "clock"
                        compact: true
                        active: root.galleryCollection === "imports"
                        onClicked: {
                            root.galleryCollection = "imports"
                            if (jobQueue)
                                jobQueue.refresh()
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                Rectangle {
                    objectName: "libraryFilterBar"
                    readonly property string visibilityRole: "always-visible|search-date-media-project-review-clear-summary"
                    Layout.fillWidth: true
                    visible: root.galleryCollection === "images" || root.galleryCollection === "groupAssets"
                    implicitHeight: libraryFilterColumn.implicitHeight + (theme.space10 * 2)
                    radius: theme.radius12
                    color: theme.alpha(theme.ink950, 0.24)
                    border.width: theme.hairlineWidth
                    border.color: theme.alpha(theme.lineSoft, 0.2)

                    ColumnLayout {
                        id: libraryFilterColumn
                        anchors.fill: parent
                        anchors.margins: theme.space8
                        spacing: theme.space6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space6

                            Rectangle {
                                id: librarySearchControl
                                objectName: "librarySearchControl"
                                readonly property string searchControlContract: "library-search-control:v1|active-frame:true|clear-visible-only-when-text:true|debounced:true|enter-applies:true|no-elide:true"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 260
                                implicitHeight: theme.controlHeightTall
                                radius: theme.radiusControl
                                color: root.librarySearchActive || root.librarySearchPending
                                       ? theme.alpha(theme.accentWarm400, 0.08)
                                       : theme.alpha(theme.glassBase, 0.28)
                                border.width: searchField.activeFocus ? 2 : 1
                                border.color: searchField.activeFocus
                                              ? theme.alpha(theme.focusRing, 0.9)
                                              : root.librarySearchPending
                                                ? theme.alpha(theme.accentWarm400, 0.7)
                                                : root.librarySearchActive
                                                  ? theme.alpha(theme.accentWarm400, 0.48)
                                                  : theme.alpha(theme.glassEdge, 0.2)

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: theme.space4
                                    anchors.rightMargin: theme.space4
                                    spacing: theme.space4

                                    AppIcon {
                                        Layout.preferredWidth: 13
                                        Layout.preferredHeight: 13
                                        name: "search"
                                        color: root.librarySearchActive || root.librarySearchPending
                                               ? theme.accentWarm400
                                               : theme.textMuted
                                    }

                                    ToneTextField {
                                        id: searchField
                                        objectName: "librarySearchField"
                                        Layout.fillWidth: true
                                        placeholderText: "Search by filename, camera, lens, location"
                                        text: root.searchDraftText
                                        background: Item {}
                                        leftPadding: 0
                                        rightPadding: 0
                                        Accessible.name: "Library search"
                                        Accessible.description: "Search by filename, camera, lens, or location."
                                        onTextChanged: {
                                            if (text !== root.searchDraftText)
                                                root.updateLibrarySearchDraft(text)
                                        }
                                        onAccepted: root.applyLibrarySearchNow()
                                    }

                                    ActionButton {
                                        id: librarySearchClearButton
                                        objectName: "librarySearchClearButton"
                                        visible: root.librarySearchHasText
                                        enabled: root.librarySearchHasText
                                        text: "Clear Search"
                                        iconName: "close"
                                        showText: false
                                        compact: true
                                        embedded: true
                                        tooltipText: "Clear library search"
                                        onClicked: root.clearLibrarySearch()
                                    }
                                }
                            }

                            ToneComboBox {
                                accessibleLabel: qsTr("Import date filter")
                                Layout.preferredWidth: 126
                                model: root.importFilterLabels
                                currentIndex: root.indexForValue(root.importFilterValues, catalogAssets.importDateFilter)
                                onActivated: catalogAssets.importDateFilter = root.importFilterValues[currentIndex]
                            }

                            ToneComboBox {
                                accessibleLabel: qsTr("Media type filter")
                                Layout.preferredWidth: 126
                                model: root.mediaFilterLabels
                                currentIndex: root.indexForValue(root.mediaFilterValues, catalogAssets.mediaTypeFilter)
                                onActivated: catalogAssets.mediaTypeFilter = root.mediaFilterValues[currentIndex]
                            }

                            ToneComboBox {
                                accessibleLabel: qsTr("Project filter")
                                Layout.preferredWidth: 142
                                model: root.projectFilterLabels
                                currentIndex: root.indexForValue(root.projectFilterValues, catalogAssets.projectFilter)
                                onActivated: root.applyProjectSelection(root.projectFilterValues[currentIndex])
                            }

                            MenuButton {
                                text: root.reviewFilterLabel()
                                iconName: "star"
                                compact: true
                                accent: theme.accentWarm400
                                tone: catalogAssets.reviewFilter === "all" ? "secondary" : "primary"
                                tooltipText: catalogAssets.reviewFilter === "all"
                                             ? "Open the menu to filter by review state."
                                             : "Show all review states. Open the menu for specific review filters."
                                menuItems: root.reviewFilterMenuItems()
                                menuWidth: 178
                                onClicked: catalogAssets.reviewFilter = "all"
                                onMenuItemTriggered: function(key) { root.handleReviewFilterMenu(key) }
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

                        RowLayout {
                            objectName: "libraryCaptureFacetRow"
                            Layout.fillWidth: true
                            spacing: theme.space6

                            Label {
                                text: "Captured"
                                color: theme.textFaint
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                                font.weight: Font.Medium
                            }

                            ToneComboBox {
                                objectName: "libraryCaptureYearFilter"
                                Layout.preferredWidth: 150
                                model: root.captureFilterLabels
                                currentIndex: root.indexForValue(root.captureFilterValues, catalogAssets.captureYearFilter)
                                Accessible.name: "Filter by capture year"
                                onActivated: catalogAssets.captureYearFilter = root.captureFilterValues[currentIndex]
                            }

                            ToneComboBox {
                                objectName: "libraryCameraFilter"
                                Layout.preferredWidth: 210
                                model: root.cameraFilterLabels
                                currentIndex: root.indexForValue(root.cameraFilterValues, catalogAssets.cameraFilter)
                                Accessible.name: "Filter by camera"
                                onActivated: catalogAssets.cameraFilter = root.cameraFilterValues[currentIndex]
                            }

                            ToneTextField {
                                id: captureDateFromField
                                objectName: "libraryCaptureDateFromField"
                                Layout.preferredWidth: 124
                                placeholderText: "From YYYY-MM-DD"
                                text: catalogAssets.captureDateFromFilter
                                Accessible.name: "Capture date from"
                                onAccepted: catalogAssets.captureDateFromFilter = text.trim()
                            }

                            ToneTextField {
                                id: captureDateToField
                                objectName: "libraryCaptureDateToField"
                                Layout.preferredWidth: 124
                                placeholderText: "To YYYY-MM-DD"
                                text: catalogAssets.captureDateToFilter
                                Accessible.name: "Capture date to"
                                onAccepted: catalogAssets.captureDateToFilter = text.trim()
                            }

                            ActionButton {
                                objectName: "libraryApplyCaptureDateRangeButton"
                                text: "Apply"
                                compact: true
                                enabled: (captureDateFromField.text.trim().length === 0 ||
                                          /^\d{4}-\d{2}-\d{2}$/.test(captureDateFromField.text.trim())) &&
                                         (captureDateToField.text.trim().length === 0 ||
                                          /^\d{4}-\d{2}-\d{2}$/.test(captureDateToField.text.trim()))
                                onClicked: {
                                    catalogAssets.captureDateFromFilter = captureDateFromField.text.trim()
                                    catalogAssets.captureDateToFilter = captureDateToField.text.trim()
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Label {
                            objectName: "libraryActiveFilterSummary"
                            Layout.fillWidth: true
                            text: root.activeFilterSummary()
                            color: root.filtersActive ? theme.textMain : theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WordWrap
                        }

                        StatusChip {
                            objectName: "libraryActiveSearchChip"
                            Layout.fillWidth: true
                            visible: root.librarySearchActive || root.librarySearchPending
                            text: root.librarySearchPending
                                  ? "Search pending: " + root.searchDraftText
                                  : "Search active: " + catalogAssets.searchFilter
                            iconName: "search"
                            tone: root.librarySearchPending ? "warning" : "accent"
                            detailText: root.librarySearchPending
                                        ? "Typing pauses briefly before the Library applies this search."
                                        : "The Library grid is filtered by this search."
                            actionText: "Clear"
                            onActionTriggered: root.clearLibrarySearch()
                        }
                    }
                }

                Rectangle {
                    objectName: "librarySelectionActionBar"
                    readonly property string visibilityRole: "contextual|batch-summary-project-export-more-clear"
                    Layout.fillWidth: true
                    visible: (root.galleryCollection === "images" || root.galleryCollection === "groupAssets") && root.selectionModeActive
                    implicitHeight: selectionActionRow.implicitHeight + (theme.space8 * 2)
                    radius: theme.radius12
                    color: theme.alpha(theme.accentCool500, 0.08)
                    border.width: theme.hairlineWidth
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
                            font.pixelSize: theme.typeBody
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: theme.typeMicro
                            elide: Text.ElideNone
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

                        MenuButton {
                            text: "More"
                            iconName: "menu"
                            compact: true
                            tooltipText: "Selection actions."
                            enabled: root.selectionModeActive || catalogAssets.selectedAssetCount > 0
                            menuItems: root.selectionActionMenuItems()
                            menuWidth: 190
                            onClicked: {
                                if (catalogAssets.selectedAssetCount > 0)
                                    catalogAssets.clearSelection()
                                else
                                    root.setSelectionModeActive(false)
                            }
                            onMenuItemTriggered: function(key) { root.handleSelectionActionMenu(key) }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    GridView {
                        id: galleryFlow
                        objectName: "libraryGalleryGrid"
                        readonly property string visibilityRole: "always-visible|asset-grid-keyboard-selection-review-open"
                        anchors.fill: parent
                        visible: (root.galleryCollection === "images" || root.galleryCollection === "timelines" || root.galleryCollection === "groupAssets") &&
                                 root.catalogViewMode === "grid" && !root.importBrowserOpen
                        clip: true
                        property int columns: Math.max(2, Math.floor(width / 256))
                        property real tileSpacing: theme.space12
                        property real tileWidth: Math.floor((width - ((columns - 1) * tileSpacing)) / columns)
                        property real tileHeight: Math.max(168, tileWidth * 0.72)
                        cellWidth: columns > 0 ? width / columns : width
                        cellHeight: tileHeight + tileSpacing
                        cacheBuffer: cellHeight * 2
                        keyNavigationEnabled: false
                        model: catalogAssets
                        boundsBehavior: Flickable.StopAtBounds

                        function updateVisibleAssetRange() {
                            if (!catalogAssets)
                                return
                            if (!visible || catalogAssets.assetCount <= 0 || columns <= 0) {
                                catalogAssets.setVisibleAssetRange(-1, -1)
                                return
                            }
                            const rowHeight = Math.max(1, cellHeight)
                            const firstRow = Math.max(0, Math.floor(contentY / rowHeight))
                            const visibleRows = Math.max(1, Math.ceil(height / rowHeight) + 1)
                            const first = Math.max(0, firstRow * columns)
                            const last = Math.min(catalogAssets.assetCount - 1, ((firstRow + visibleRows) * columns) - 1)
                            catalogAssets.setVisibleAssetRange(first, last)
                        }

                        Component.onCompleted: updateVisibleAssetRange()
                        onCountChanged: updateVisibleAssetRange()
                        onContentYChanged: updateVisibleAssetRange()
                        onHeightChanged: updateVisibleAssetRange()
                        onWidthChanged: updateVisibleAssetRange()
                        onVisibleChanged: updateVisibleAssetRange()
                        onColumnsChanged: updateVisibleAssetRange()
                        onCellHeightChanged: updateVisibleAssetRange()

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

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
                            required property string adjustmentPatchJson
                            required property int rating
                            required property bool picked
                            required property bool rejected
                            required property string colorLabel
                            required property string cullStatus
                            required property int burstSize
                            required property int burstPosition
                            required property int duplicateGroupSize
                            required property bool bestShotSuggested
                            required property int syncGroupSize
                            required property bool online
                            readonly property bool selected: catalogAssets.assetSelected(assetId)

                            width: galleryFlow.tileWidth
                            height: galleryFlow.tileHeight
                            activeFocusOnTab: true

                            Keys.onReturnPressed: root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, adjustmentPatchJson, mediaType)
                            Keys.onEnterPressed: root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, adjustmentPatchJson, mediaType)
                            Keys.onSpacePressed: root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, adjustmentPatchJson, mediaType, Qt.ControlModifier)

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
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 4
                                    visible: galleryTile.colorLabel.length > 0
                                    color: galleryTile.colorLabel === "red" ? "#ef6666"
                                         : galleryTile.colorLabel === "yellow" ? "#e8bd55"
                                         : galleryTile.colorLabel === "green" ? "#62c88a"
                                         : galleryTile.colorLabel === "blue" ? "#67a8ed"
                                         : "#ae7de0"
                                    z: 4
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: selected ? theme.alpha(theme.accentCool500, 0.12) : theme.alpha(theme.accentCool500, 0.04) }
                                        GradientStop { position: 1.0; color: theme.alpha(theme.ink950, selected ? 0.14 : 0.18) }
                                    }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: theme.space2
                                    radius: Math.max(1, parent.radius - 2)
                                    visible: selected
                                    color: "transparent"
                                    border.width: theme.hairlineWidth
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
                                    radius: theme.radius12
                                    color: selected ? theme.accentCool400 : theme.alpha(theme.ink950, 0.62)
                                    border.width: theme.hairlineWidth
                                    border.color: selected
                                                  ? theme.alpha(theme.accentCool300, 0.92)
                                                  : theme.alpha(theme.lineStrong, 0.42)

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 8
                                        height: 8
                                        radius: theme.radius4
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
                                        font.pixelSize: theme.typeMicro
                                        font.weight: Font.DemiBold
                                    }
                                }

                                Rectangle {
                                    id: mediaTypeBadge
                                    readonly property string mediaKind: mediaType
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.topMargin: theme.space8
                                    anchors.leftMargin: root.selectionModeActive ? theme.space8 + 32 : theme.space8
                                    visible: mediaType !== "photo"
                                    radius: theme.radiusChip
                                    color: theme.alpha(theme.ink950, 0.66)
                                    border.width: theme.hairlineWidth
                                    border.color: theme.alpha(theme.lineSoft, 0.48)
                                    implicitWidth: mediaBadge.implicitWidth + theme.space12
                                    implicitHeight: 22

                                    RowLayout {
                                        id: mediaBadge
                                        anchors.centerIn: parent
                                        spacing: theme.space4

                                        MediaTypeIcon {
                                            mediaType: String(mediaTypeBadge.mediaKind)
                                            color: theme.textStrong
                                            Layout.preferredWidth: theme.scaled(14)
                                            Layout.preferredHeight: theme.scaled(14)
                                        }

                                        Label {
                                            text: mediaTypeBadge.mediaKind === "timeline" ? "Timeline" : "Video"
                                            color: theme.textStrong
                                            font.family: theme.fontMono
                                            font.pixelSize: theme.typeMicro
                                        }
                                    }
                                }

                                Rectangle {
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: theme.space8
                                    visible: mediaType !== "photo" && durationText.length > 0
                                    radius: theme.radiusChip
                                    color: theme.alpha(theme.ink950, 0.68)
                                    border.width: theme.hairlineWidth
                                    border.color: theme.alpha(theme.lineSoft, 0.48)
                                    implicitWidth: durationBadge.implicitWidth + theme.space12
                                    implicitHeight: 22

                                    Label {
                                        id: durationBadge
                                        anchors.centerIn: parent
                                        text: durationText
                                        color: theme.textStrong
                                        font.family: theme.fontMono
                                        font.pixelSize: theme.typeMicro
                                    }
                                }

                                MouseArea {
                                    id: thumbnailMouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: function(mouse) {
                                        galleryTile.forceActiveFocus()
                                        root.selectCatalogAsset(index, mouse.modifiers)
                                    }
                                    onDoubleClicked: {
                                        if (root.selectionModeActive)
                                            return
                                        galleryTile.forceActiveFocus()
                                        root.selectCatalogAsset(index);
                                        root.openSelectedAsset(mediaType === "photo" ? "edit" : "timeline");
                                    }
                                }
                            }
                        }
                }
            }

                    Item {
                        objectName: "libraryRecentImportsCollection"
                        anchors.fill: parent
                        visible: root.galleryCollection === "imports" && !root.importBrowserOpen
                        clip: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: theme.space12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                Label {
                                    text: "Recent Import Sessions"
                                    color: theme.textStrong
                                    font.family: theme.fontDisplay
                                    font.pixelSize: theme.typeSection
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Each import stays together with its source, date, result, and a direct link to the media it added."
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeBody
                                    wrapMode: Text.WordWrap
                                }
                            }

                            GridView {
                                id: recentImportSessionsGrid
                                objectName: "libraryRecentImportSessionsGrid"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                cellWidth: Math.max(1, Math.min(width,
                                                               Math.max(theme.scaled(280),
                                                                        Math.floor(width / Math.max(1, Math.floor(width / theme.scaled(330)))))))
                                cellHeight: theme.scaled(224)
                                model: root.recentImportSessions
                                visible: root.recentImportSessions.length > 0
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    readonly property var sessionMetadata: modelData.metadata || ({})
                                    readonly property var importReceipt: sessionMetadata.importResult || ({})
                                    readonly property int importedCount: Number(importReceipt.importedCount || 0)

                                    width: Math.max(1, recentImportSessionsGrid.cellWidth - theme.space12)
                                    height: recentImportSessionsGrid.cellHeight - theme.space12
                                    radius: theme.radiusPanel
                                    color: importSessionHover.hovered
                                           ? theme.alpha(theme.ink800, 0.96)
                                           : theme.alpha(theme.ink900, 0.86)
                                    border.width: theme.hairlineWidth
                                    border.color: importSessionHover.hovered
                                                  ? theme.alpha(theme.accentCool400, 0.48)
                                                  : theme.alpha(theme.lineSoft, 0.3)

                                    HoverHandler { id: importSessionHover }

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: theme.space12
                                        spacing: theme.space8

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: theme.space8

                                            Rectangle {
                                                Layout.preferredWidth: theme.scaled(38)
                                                Layout.preferredHeight: theme.scaled(38)
                                                radius: theme.radius12
                                                color: theme.alpha(theme.accentCool400, 0.12)
                                                border.width: theme.hairlineWidth
                                                border.color: theme.alpha(theme.accentCool400, 0.28)

                                                AppIcon {
                                                    anchors.centerIn: parent
                                                    name: "source"
                                                    color: theme.accentCool300
                                                    implicitWidth: theme.scaled(18)
                                                    implicitHeight: theme.scaled(18)
                                                }
                                            }

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: theme.space2

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: String(modelData.displayLabel || root.fileNameForPath(String(modelData.sourcePath || "")) || "Imported folder")
                                                    color: theme.textStrong
                                                    font.family: theme.fontUi
                                                    font.pixelSize: theme.typeTitle
                                                    font.weight: Font.DemiBold
                                                    maximumLineCount: 1
                                                    elide: Text.ElideRight
                                                }

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: {
                                                        const date = new Date(String(modelData.createdAt || ""))
                                                        return isNaN(date.getTime()) ? "Import date unavailable" : date.toLocaleString()
                                                    }
                                                    color: theme.textFaint
                                                    font.family: theme.fontMono
                                                    font.pixelSize: theme.typeMicro
                                                    maximumLineCount: 1
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: String(modelData.sourcePath || "Source folder unavailable")
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                            wrapMode: Text.WrapAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: root.importSessionSummary(modelData)
                                            color: modelData.status === "Completed" ? theme.success
                                                 : modelData.status === "Failed" ? theme.error
                                                 : theme.accentWarm400
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                            font.weight: Font.DemiBold
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                        }

                                        Item { Layout.fillHeight: true }

                                        ActionButton {
                                            Layout.fillWidth: true
                                            text: "View Imported Media"
                                            iconName: "grid"
                                            compact: true
                                            enabled: modelData.status === "Completed" && importedCount > 0
                                            onClicked: root.openImportSession(modelData)
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: root.recentImportSessions.length === 0
                                spacing: theme.space8

                                Item { Layout.fillHeight: true }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Your import sessions will appear here."
                                    color: theme.textStrong
                                    font.family: theme.fontDisplay
                                    font.pixelSize: theme.typeSection
                                    horizontalAlignment: Text.AlignHCenter
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Import a folder to keep each new batch easy to find and review."
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeBody
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                }

                                ActionButton {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "Choose Folder"
                                    iconName: "folder"
                                    tone: "primary"
                                    onClicked: root.chooseImportFolder()
                                }

                                Item { Layout.fillHeight: true }
                            }
                        }
                    }

                    Loader {
                        objectName: "libraryReviewLoader"
                        readonly property string visibilityRole: "contextual|compare-survey-review-cull"
                        anchors.fill: parent
                        visible: (root.galleryCollection === "images" || root.galleryCollection === "timelines" || root.galleryCollection === "groupAssets") &&
                                 !root.importBrowserOpen && root.catalogViewMode !== "grid"
                        active: visible
                        sourceComponent: root.catalogViewMode === "compare" ? compareView : surveyView
                    }

                    Item {
                        objectName: "libraryCarouselsCollection"
                        anchors.fill: parent
                        visible: root.galleryCollection === "carousels" && !root.importBrowserOpen
                        clip: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: theme.space12

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space4

                                    Label {
                                        text: "Saved Boards"
                                        color: theme.textStrong
                                        font.family: theme.fontDisplay
                                        font.pixelSize: theme.typeSection
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Open a saved design or start a new set of boards."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeBody
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                ActionButton {
                                    objectName: "libraryNewCarouselButton"
                                    text: "New Boards"
                                    iconName: "plus"
                                    tone: "primary"
                                    onClicked: root.requestNewCarousel()
                                }
                            }

                            GridView {
                                id: carouselDocumentsGrid
                                objectName: "libraryCarouselDocumentsGrid"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                cellWidth: Math.max(1, Math.min(width,
                                                               Math.max(theme.scaled(244),
                                                                        Math.floor(width / Math.max(1, Math.floor(width / theme.scaled(300)))))))
                                cellHeight: theme.scaled(272)
                                model: root.carouselLibraryRecords
                                visible: root.carouselLibraryRecords.length > 0
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index

                                    width: Math.max(1, carouselDocumentsGrid.cellWidth - theme.space12)
                                    height: carouselDocumentsGrid.cellHeight - theme.space12
                                    radius: theme.radius12
                                    color: carouselCardHover.hovered
                                           ? theme.alpha(theme.ink800, 0.96)
                                           : theme.alpha(theme.ink900, 0.9)
                                    border.width: theme.hairlineWidth
                                    border.color: carouselCardHover.hovered
                                                  ? theme.alpha(theme.accentCool400, 0.5)
                                                  : theme.alpha(theme.lineSoft, 0.42)

                                    HoverHandler { id: carouselCardHover }

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: theme.space12
                                        spacing: theme.space10

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: theme.scaled(104)
                                            radius: theme.radius8
                                            color: theme.alpha(theme.surfaceInset, 0.84)
                                            border.width: theme.hairlineWidth
                                            border.color: theme.alpha(theme.lineSoft, 0.38)

                                            RowLayout {
                                                anchors.centerIn: parent
                                                spacing: theme.space6

                                                Repeater {
                                                    model: Math.min(3, Math.max(1, Number(modelData.slideCount || 1)))

                                                    Rectangle {
                                                        required property int index
                                                        Layout.preferredWidth: theme.scaled(42)
                                                        Layout.preferredHeight: theme.scaled(62)
                                                        radius: theme.radius4
                                                        color: index === 1
                                                               ? theme.alpha(theme.accentCool400, 0.12)
                                                               : theme.alpha(theme.textStrong, 0.045)
                                                        border.width: theme.hairlineWidth
                                                        border.color: index === 1
                                                                      ? theme.alpha(theme.accentCool400, 0.6)
                                                                      : theme.alpha(theme.lineSoft, 0.7)

                                                        AppIcon {
                                                            anchors.centerIn: parent
                                                            name: "carousel"
                                                            color: theme.alpha(theme.textMuted, index === 1 ? 0.9 : 0.56)
                                                            implicitWidth: theme.scaled(16)
                                                            implicitHeight: theme.scaled(16)
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: theme.space6

                                            MediaTypeIcon {
                                                mediaType: "carousel"
                                                color: theme.textMuted
                                                Layout.preferredWidth: theme.scaled(16)
                                                Layout.preferredHeight: theme.scaled(16)
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: "CAROUSEL"
                                                color: theme.textMuted
                                                font.family: theme.fontMono
                                                font.pixelSize: theme.typeMicro
                                                font.letterSpacing: 0.8
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: String(modelData.name || "Untitled Boards")
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeTitle
                                            font.weight: Font.DemiBold
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: {
                                                const format = String(modelData.documentFormat || "4x5").toUpperCase()
                                                const slides = Number(modelData.slideCount || 0)
                                                const updatedAt = String(modelData.updatedAt || modelData.createdAt || "")
                                                const readableDate = updatedAt.length > 0
                                                                     ? updatedAt.replace("T", " ").slice(0, 16)
                                                                     : "Not saved yet"
                                                return format + "  ·  " + slides + " board" + (slides === 1 ? "" : "s")
                                                       + "  ·  " + readableDate
                                            }
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeMicro
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                        }

                                        Item { Layout.fillHeight: true }

                                        ActionButton {
                                            Layout.fillWidth: true
                                            objectName: "libraryOpenBoardsButton" + String(modelData.documentId || "")
                                            text: "Open Boards"
                                            iconName: "chevron-right"
                                            compact: true
                                            onClicked: root.requestOpenCarousel(String(modelData.documentId || ""))
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: root.carouselLibraryRecords.length === 0
                                spacing: theme.space8

                                Item { Layout.fillHeight: true }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Your saved board documents will live here."
                                    color: theme.textStrong
                                    font.family: theme.fontDisplay
                                    font.pixelSize: theme.typeSection
                                    horizontalAlignment: Text.AlignHCenter
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Keep multi-board photo stories alongside the rest of your creative library."
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeBody
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                }

                                ActionButton {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: "Create boards"
                                    iconName: "plus"
                                    tone: "primary"
                                    onClicked: root.requestNewCarousel()
                                }

                                Item { Layout.fillHeight: true }
                            }
                        }
                    }

                    Item {
                        objectName: "libraryProjectsCollection"
                        anchors.fill: parent
                        visible: root.galleryCollection === "projects" && !root.importBrowserOpen
                        clip: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: theme.space12

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space4

                                    Label {
                                        text: "Groups"
                                        color: theme.textStrong
                                        font.family: theme.fontDisplay
                                        font.pixelSize: theme.typeSection
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Keep related photos, videos, and timelines together. Items can belong to more than one group."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeBody
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                ToneTextField {
                                    id: newGalleryProjectName
                                    objectName: "libraryNewProjectNameField"
                                    Layout.preferredWidth: theme.scaled(220)
                                    placeholderText: "New group name"
                                    Accessible.name: "New group name"
                                    onAccepted: {
                                        root.createAndFocusProject(text)
                                        text = ""
                                    }
                                }

                                ActionButton {
                                    objectName: "libraryCreateProjectButton"
                                    text: "Create Group"
                                    iconName: "plus"
                                    compact: true
                                    enabled: newGalleryProjectName.text.trim().length > 0
                                    onClicked: {
                                        root.createAndFocusProject(newGalleryProjectName.text)
                                        newGalleryProjectName.text = ""
                                    }
                                }
                            }

                            GridView {
                                id: projectCollectionsGrid
                                objectName: "libraryProjectCollectionsGrid"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                cellWidth: Math.max(1, Math.min(width,
                                                               Math.max(theme.scaled(228),
                                                                        Math.floor(width / Math.max(1, Math.floor(width / theme.scaled(280)))))))
                                cellHeight: theme.scaled(180)
                                model: root.projectCollectionNames
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                delegate: Button {
                                    required property string modelData
                                    required property int index
                                    id: projectCollectionCard

                                    readonly property int memberCount: catalogAssets.assetCountForProject(
                                                                          modelData === "Unassigned" ? "unassigned" : modelData)

                                    width: Math.max(1, projectCollectionsGrid.cellWidth - theme.space12)
                                    height: projectCollectionsGrid.cellHeight - theme.space12
                                    hoverEnabled: true
                                    activeFocusOnTab: true
                                    Accessible.name: "Open " + modelData + " group"
                                    Accessible.description: memberCount + " media " + (memberCount === 1 ? "item" : "items") +
                                                            (modelData === "Unassigned"
                                                             ? " outside a project."
                                                             : " in the " + modelData + " group.")
                                    onClicked: root.openProjectCollection(modelData === "Unassigned" ? "unassigned" : modelData)

                                    background: Rectangle {
                                        radius: theme.radius12
                                        color: projectCollectionCard.down
                                               ? theme.alpha(theme.ink750, 0.96)
                                               : projectCollectionCard.hovered
                                                 ? theme.alpha(theme.ink800, 0.96)
                                                 : theme.alpha(theme.ink900, 0.9)
                                        border.width: projectCollectionCard.visualFocus ? theme.focusBorderWidth : theme.hairlineWidth
                                        border.color: projectCollectionCard.visualFocus
                                                      ? theme.focusRing
                                                      : projectCollectionCard.hovered
                                                        ? theme.alpha(theme.accentCool400, 0.5)
                                                        : theme.alpha(theme.lineSoft, 0.42)
                                    }

                                    contentItem: ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: theme.space14
                                        spacing: theme.space8

                                        Rectangle {
                                            Layout.preferredWidth: theme.scaled(40)
                                            Layout.preferredHeight: theme.scaled(40)
                                            radius: theme.radius10
                                            color: theme.alpha(theme.accentCool400, 0.12)
                                            border.width: theme.hairlineWidth
                                            border.color: theme.alpha(theme.accentCool400, 0.3)

                                            AppIcon {
                                                anchors.centerIn: parent
                                            name: "folder"
                                                color: theme.accentCool300
                                                implicitWidth: theme.scaled(18)
                                                implicitHeight: theme.scaled(18)
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeTitle
                                            font.weight: Font.DemiBold
                                            maximumLineCount: 1
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: String(projectCollectionCard.memberCount) +
                                                  (projectCollectionCard.memberCount === 1 ? " item" : " items") +
                                                  (modelData === "Unassigned" ? " · outside a project" : " · in this project")
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                        }
                                    }
                                }
                            }

                            Label {
                                visible: catalogAssets.projectNames.length === 0
                                Layout.fillWidth: true
                                text: "No groups yet. Create one above; unassigned images stay available here."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeBody
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    Item {
                        id: libraryLoadingSkeleton
                        objectName: "libraryLoadingSkeleton"
                        anchors.fill: parent
                        visible: (root.galleryCollection === "images" || root.galleryCollection === "timelines" || root.galleryCollection === "groupAssets") && catalogAssets.loading &&
                                 catalogAssets.assetCount === 0 && !root.importBrowserOpen
                        z: 1

                        LooksyLoadingScene {
                            objectName: "libraryLoadingScene"
                            anchors.centerIn: parent
                            width: Math.max(100, Math.min(560, parent.width - 40, (parent.height - 90) * 1.5))
                            scene: "library"
                            title: "Opening your library"
                            detail: "Loading photos and saved edits."
                            active: libraryLoadingSkeleton.visible
                        }
                    }

                    Rectangle {
                        id: libraryEmptyArtworkHost
                        objectName: "libraryEmptyState"
                        readonly property string visibilityRole: "contextual|empty-filter-reset-import-handoff"
                        anchors.fill: parent
                        visible: (root.galleryCollection === "images" || root.galleryCollection === "timelines" || root.galleryCollection === "groupAssets") && catalogAssets.assetCount === 0 &&
                                 !catalogAssets.loading && !root.importBrowserOpen
                        radius: theme.radiusSurface
                        color: theme.alpha(theme.ink900, 0.9)
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.24)

                        Column {
                            anchors.centerIn: parent
                            width: Math.max(100, Math.min(600, parent.width - 40))
                            spacing: theme.space12

                            LooksyLoadingScene {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.max(100, Math.min(520, libraryEmptyArtworkHost.width - 40, (libraryEmptyArtworkHost.height - 100) * 1.5))
                                scene: "library"
                                active: false
                                showProgress: false
                            }

                            Label {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: root.galleryCollection === "timelines"
                                      ? "No saved timelines yet."
                                      : root.galleryCollection === "groupAssets"
                                        ? "This group is empty."
                                      : catalogAssets.totalAssetCount === 0
                                        ? "No imported images yet."
                                        : "No images match the current filters."
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: theme.typePageTitle
                                font.weight: Font.DemiBold
                            }

                            Label {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: root.galleryCollection === "timelines"
                                      ? "Create or save a timeline and it will appear in this collection."
                                      : root.galleryCollection === "groupAssets"
                                        ? "Add images or timelines to this group from the Inspector or selection actions."
                                      : catalogAssets.totalAssetCount === 0
                                        ? "Switch to Import to bring photos and videos in from folders, cards, and drives."
                                        : "Broaden the search or reset a few filters to reopen the working set."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeBody
                            }
                        }
                    }

                    Rectangle {
                        objectName: "libraryImportBrowser"
                        readonly property string visibilityRole: "contextual|folder-pick-preview-import-jobs-close"
                        anchors.fill: parent
                        visible: root.importBrowserOpen
                        radius: theme.radiusSurface
                        color: theme.alpha(theme.ink950, 0.98)
                        border.width: theme.hairlineWidth
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
                                    spacing: theme.space4

                                    Label {
                                        text: "Import From Finder"
                                        color: theme.textStrong
                                        font.family: theme.fontDisplay
                                        font.pixelSize: theme.typePageTitle
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        text: root.localFolderPath.length > 0
                                              ? root.localFolderPath
                                              : "Choose a folder, preview it, then hand it off to Import."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeBody
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 2
                                        elide: Text.ElideNone
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

                                SplitButton {
                                    text: root.hasActiveImportJobs() ? "Importing" : "Import This Folder"
                                    tone: "primary"
                                    iconName: "folder"
                                    primaryEnabled: root.localFolderPath.length > 0 && !catalogAssets.busy
                                    disabledReason: root.localFolderPath.length > 0
                                                    ? "Import is already working."
                                                    : "Choose a folder first."
                                    menuItems: root.importActionMenuItems()
                                    menuTooltipText: "Import options"
                                    onClicked: root.importCurrentFolder()
                                    onMenuItemTriggered: function(key) { root.handleImportMenuAction(key) }
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
                                font.pixelSize: theme.typeMicro
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

                                delegate: ImportFolderChip {
                                    onActivated: function(folderUrl) { root.openLocalFolder(folderUrl) }
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

                                delegate: ImportImageTile {
                                    selected: root.localSelectedFilePath === filePath
                                    onActivated: function(filePath) { root.selectLocalFile(filePath) }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "libraryInspectorPanel"
            readonly property string surfaceContract: "library-inspector:v1|preview:always|metadata:selection|review:single-selection|source-actions:selection|batch-summary:multi-selection|no-elide:true|no-overlap:true"
            Layout.preferredWidth: 332
            Layout.fillHeight: true
            visible: root.galleryCollection === "images" || root.galleryCollection === "timelines" || root.galleryCollection === "groupAssets"
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
                        font.pixelSize: theme.typePageTitle
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
                        font.pixelSize: theme.typeBody
                        font.weight: Font.Medium
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideNone
                    }

                    Rectangle {
                        id: inspectorPreviewCard
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.hasSelection && !catalogAssets.hasMultiSelection
                                                ? Math.max(112, Math.min(420,
                                                                        width / selectedInspectorPreview.sourceAspectRatio))
                                                : 152
                        visible: true
                        radius: theme.radius12
                        color: theme.ink850
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.3)
                        clip: true

                        Image {
                            id: selectedInspectorPreview
                            readonly property real sourceAspectRatio: status === Image.Ready && implicitHeight > 0
                                                                      ? implicitWidth / implicitHeight
                                                                      : 1.35
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
                                font.pixelSize: theme.typeHero
                                font.weight: Font.Bold
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 248
                                horizontalAlignment: Text.AlignHCenter
                                text: !root.hasSelection ? "Select media from the grid to inspect it." : root.selectionSummaryText()
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeBody
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

                    Loader {
                        objectName: "librarySelectedInspectorDetailsLoader"
                        Layout.fillWidth: true
                        active: root.hasSelection && !catalogAssets.hasMultiSelection

                        sourceComponent: Component {
                            ColumnLayout {
                                spacing: theme.space8

                                InspectorRule {}

                                Label {
                                    Layout.fillWidth: true
                                    text: "Details"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeLabel
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
                                    font.pixelSize: theme.typeMicro
                                    maximumLineCount: 2
                                    elide: Text.ElideNone
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }
                    }

                    Loader {
                        objectName: "librarySelectedInspectorReviewLoader"
                        Layout.fillWidth: true
                        active: root.hasSelection && !catalogAssets.hasMultiSelection

                        sourceComponent: Component {
                            ColumnLayout {
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
                                        font.pixelSize: theme.typeLabel
                                        font.weight: Font.Bold
                                    }

                                    Label {
                                        text: (catalogAssets.selectedIndex + 1) + " / " + catalogAssets.assetCount
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: theme.typeMicro
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

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space6

                                    Repeater {
                                        model: [
                                            { key: "red", label: "Red", shortcut: "6", color: "#ef6666" },
                                            { key: "yellow", label: "Yellow", shortcut: "7", color: "#e8bd55" },
                                            { key: "green", label: "Green", shortcut: "8", color: "#62c88a" },
                                            { key: "blue", label: "Blue", shortcut: "9", color: "#67a8ed" },
                                            { key: "purple", label: "Purple", shortcut: "Alt+9", color: "#ae7de0" }
                                        ]

                                        delegate: ActionButton {
                                            required property var modelData

                                            Layout.fillWidth: true
                                            compact: true
                                            text: modelData.shortcut
                                            accent: modelData.color
                                            active: catalogAssets.selectedColorLabel === modelData.key
                                            enabled: catalogAssets.selectedIndex >= 0
                                            tooltipText: modelData.label + " label (" + modelData.shortcut + ")"
                                            Accessible.name: modelData.label + " color label"
                                            onClicked: root.applyColorLabelReview(modelData.key, false)
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
                        }
                    }

                    Loader {
                        objectName: "librarySelectedInspectorSourceLoader"
                        Layout.fillWidth: true
                        active: root.hasSelection && !catalogAssets.hasMultiSelection

                        sourceComponent: Component {
                            ColumnLayout {
                                spacing: theme.space8

                                InspectorRule {}

                                Label {
                                    Layout.fillWidth: true
                                    text: catalogAssets.selectedOnline
                                          ? "Source available on disk."
                                          : "Source offline. Recheck mounted drives or relink it."
                                    color: catalogAssets.selectedOnline ? theme.textMuted : theme.warning
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeCaption
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
                        }
                    }

                    Item { Layout.preferredHeight: theme.space10 }
                }
            }
        }
    }
}
