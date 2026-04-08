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

    signal assetActivated(string destination)
    signal projectActivated(string destination, string projectName)
    signal localAssetActivated(string filePath, string previewSource, string displayName)

    property string projectTarget: ""
    property bool importBrowserOpen: false
    property string localFolderUrl: ""
    property string localFolderPath: ""
    property string localSelectedFilePath: ""
    property string catalogViewMode: "grid"
    property bool focusCheckEnabled: false

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

    function selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType) {
        catalogAssets.selectIndex(index);
        if (mediaType === "photo")
            editorPreview.loadAsset(assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson);
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

    function selectLocalFile(index) {
        localSelectedFilePath = localImageModel.get(index, "filePath");
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

    function reviewShortcutsEnabled() {
        return !root.importBrowserOpen && !catalogAssets.busy && !newProjectField.activeFocus
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
                        selected: modelData.assetId === catalogAssets.selectedAssetId
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
                        selected: modelData.assetId === catalogAssets.selectedAssetId
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
            border.color: theme.alpha(theme.lineSoft, 0.28)

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
                    Layout.preferredHeight: 194
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink900, 0.92)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.52)

                    gradient: Gradient {
                        GradientStop { position: 0.0; color: theme.alpha(theme.accentWarm500, 0.08) }
                        GradientStop { position: 0.24; color: theme.alpha(theme.accentCool500, 0.04) }
                        GradientStop { position: 1.0; color: theme.alpha(theme.ink900, 0.98) }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: theme.space16
                        spacing: theme.space10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space16

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Label {
                                    text: "Library"
                                    color: theme.textStrong
                                    font.family: theme.fontDisplay
                                    font.pixelSize: 32
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    text: catalogAssets.assetCount + " visible  •  "
                                          + catalogAssets.totalAssetCount + " total  •  "
                                          + catalogAssets.driveCount + " drives"
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                }

                                Label {
                                    text: root.activeFilterSummary()
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }

                            RowLayout {
                                spacing: theme.space8

                                ActionButton {
                                    text: "Grid"
                                    compact: true
                                    active: root.catalogViewMode === "grid"
                                    onClicked: root.catalogViewMode = "grid"
                                }

                                ActionButton {
                                    text: "Compare"
                                    compact: true
                                    active: root.catalogViewMode === "compare"
                                    enabled: catalogAssets.assetCount > 0
                                    onClicked: root.catalogViewMode = "compare"
                                }

                                ActionButton {
                                    text: "Survey"
                                    compact: true
                                    active: root.catalogViewMode === "survey"
                                    enabled: catalogAssets.assetCount > 0
                                    onClicked: root.catalogViewMode = "survey"
                                }

                                ActionButton {
                                    text: "Finder Import"
                                    tone: "primary"
                                    onClicked: root.chooseImportFolder()
                                }

                                ActionButton {
                                    text: catalogAssets.loading ? "Refreshing..." : "Reload"
                                    enabled: !catalogAssets.busy
                                    onClicked: catalogAssets.refresh()
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space8

                                ToneTextField {
                                    id: searchField
                                    Layout.fillWidth: true
                                    placeholderText: "Search by filename, camera, lens, location..."
                                    text: catalogAssets.searchFilter
                                    onTextChanged: catalogAssets.searchFilter = text
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
                                    compact: true
                                    enabled: catalogAssets.searchFilter !== ""
                                             || catalogAssets.importDateFilter !== "all"
                                             || catalogAssets.captureYearFilter !== "all"
                                             || catalogAssets.projectFilter !== "all"
                                             || catalogAssets.mediaTypeFilter !== "all"
                                             || catalogAssets.reviewFilter !== "all"
                                             || catalogAssets.availabilityFilter !== "all"
                                             || catalogAssets.minimumRatingFilter > 0
                                    onClicked: root.clearFilters()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space8

                                ToneComboBox {
                                    Layout.preferredWidth: 160
                                    model: root.importFilterLabels
                                    currentIndex: root.indexForValue(root.importFilterValues, catalogAssets.importDateFilter)
                                    onActivated: catalogAssets.importDateFilter = root.importFilterValues[currentIndex]
                                }

                                ToneComboBox {
                                    Layout.preferredWidth: 160
                                    model: root.mediaFilterLabels
                                    currentIndex: root.indexForValue(root.mediaFilterValues, catalogAssets.mediaTypeFilter)
                                    onActivated: catalogAssets.mediaTypeFilter = root.mediaFilterValues[currentIndex]
                                }

                                ToneComboBox {
                                    Layout.preferredWidth: 180
                                    model: root.projectFilterLabels
                                    currentIndex: root.indexForValue(root.projectFilterValues, catalogAssets.projectFilter)
                                    onActivated: root.applyProjectSelection(root.projectFilterValues[currentIndex])
                                }

                                Item {
                                    Layout.fillWidth: true
                                }
                            }
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

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        Flow {
                            id: galleryFlow
                            width: galleryFlick.width
                            spacing: theme.space10

                            property int columns: Math.max(2, Math.floor(width / 248))
                            property real tileWidth: Math.floor((width - ((columns - 1) * spacing)) / columns)

                            Repeater {
                                model: catalogAssets

                                delegate: Item {
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

                                    width: galleryFlow.tileWidth
                                    height: previewFrame.height

                                    Rectangle {
                                        id: previewFrame
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        readonly property real sourceAspectRatio: previewImage.sourceSize.width > 0 && previewImage.sourceSize.height > 0
                                                                                 ? previewImage.sourceSize.height / previewImage.sourceSize.width
                                                                                 : 0.68
                                        height: Math.max(148, width * sourceAspectRatio)
                                        radius: theme.radius12
                                        color: theme.alpha(theme.ink900, 0.94)
                                        clip: true

                                        Rectangle {
                                            anchors.fill: parent
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: theme.alpha(theme.accentCool500, 0.04) }
                                                GradientStop { position: 1.0; color: theme.alpha(theme.ink950, 0.18) }
                                            }
                                        }

                                        Image {
                                            id: previewImage
                                            anchors.fill: parent
                                            source: thumbnailSource
                                            asynchronous: true
                                            cache: false
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                            sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                                            opacity: online ? 1.0 : 0.42
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            color: theme.alpha(theme.ink950, thumbnailMouseArea.containsMouse ? 0.04 : 0.0)
                                        }

                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.margins: theme.space8
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
                                            onClicked: root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType)
                                            onDoubleClicked: {
                                                root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType);
                                                root.assetActivated(mediaType === "photo" ? "edit" : "timeline");
                                            }
                                        }
                                    }

                                    Rectangle {
                                        anchors.fill: previewFrame
                                        radius: previewFrame.radius
                                        color: "transparent"
                                        border.width: catalogAssets.selectedIndex === index ? 2 : 1
                                        border.color: catalogAssets.selectedIndex === index
                                                      ? theme.accentWarm400
                                                      : theme.alpha(theme.lineSoft, 0.42)
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
                        color: theme.alpha(theme.ink900, 0.92)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.42)

                        Column {
                            anchors.centerIn: parent
                            spacing: theme.space10

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

                                    width: Math.max(108, folderLabel.implicitWidth + theme.space20)
                                    height: 34
                                    radius: theme.radius8
                                    color: theme.alpha(theme.ink900, 0.92)
                                    border.width: 1
                                    border.color: theme.alpha(theme.lineSoft, 0.48)

                                    Label {
                                        id: folderLabel
                                        anchors.centerIn: parent
                                        text: localFolderModel.get(index, "fileName")
                                        color: theme.textMain
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        font.weight: Font.Medium
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.openLocalFolder(localFolderModel.get(index, "fileUrl"))
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

                                    width: 204
                                    height: 206
                                    radius: theme.radius12
                                    color: theme.alpha(theme.ink900, 0.94)
                                    border.width: root.localSelectedFilePath === localImageModel.get(index, "filePath") ? 1 : 0
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
                                                source: localImageModel.get(index, "fileUrl")
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                                cache: false
                                            }
                                        }

                                        Label {
                                            text: localImageModel.get(index, "fileName")
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.selectLocalFile(index)
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
            radius: theme.radiusSurface
            color: theme.ink850
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.28)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.space12
                spacing: theme.space12

                BrandPanel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 350
                    title: "Details"
                    subtitle: catalogAssets.selectedAssetId.length > 0
                              ? root.fileNameForPath(catalogAssets.selectedFilePath)
                              : "Select an image to inspect its metadata here instead of on the gallery."
                    accent: theme.accentWarm400

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 176
                        radius: theme.radius12
                        color: theme.alpha(theme.ink900, 0.94)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.48)
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: catalogAssets.selectedThumbnailSource
                            asynchronous: true
                            cache: false
                            fillMode: Image.PreserveAspectFit
                            opacity: catalogAssets.selectedAssetId.length > 0 ? (catalogAssets.selectedOnline ? 1.0 : 0.42) : 0.0
                        }

                        Column {
                            anchors.centerIn: parent
                            visible: catalogAssets.selectedAssetId.length === 0
                            spacing: theme.space8

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "No Selection"
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Metadata appears here when you pick a frame."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 11
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.space8

                        InfoPill {
                            Layout.fillWidth: true
                            label: "Media"
                            value: catalogAssets.selectedAssetId.length > 0
                                   ? (catalogAssets.selectedMediaType === "timeline" ? "Timeline" : catalogAssets.selectedMediaType)
                                   : "None"
                            accent: theme.accentCool400
                        }

                        InfoPill {
                            Layout.fillWidth: true
                            label: "Source"
                            value: catalogAssets.selectedAssetId.length > 0 ? root.availabilityLabel(catalogAssets.selectedOnline) : "Waiting"
                            accent: catalogAssets.selectedOnline ? theme.success : theme.warning
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: catalogAssets.selectedCameraLabel.length > 0 ? catalogAssets.selectedCameraLabel : "No camera metadata for the current selection."
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: catalogAssets.selectedCaptureDate.length > 0 ? catalogAssets.selectedCaptureDate : "Capture date unavailable"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 11
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: catalogAssets.selectedLensModel.length > 0
                        text: catalogAssets.selectedLensModel
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: catalogAssets.selectedProjectSummary.length > 0
                        text: "Projects  •  " + catalogAssets.selectedProjectSummary
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: catalogAssets.selectedImportedAt.length > 0
                        text: "Imported  •  " + catalogAssets.selectedImportedAt
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: catalogAssets.selectedFilePath.length > 0 ? catalogAssets.selectedFilePath : "No file path recorded."
                        color: theme.textFaint
                        font.family: theme.fontMono
                        font.pixelSize: 10
                        wrapMode: Text.WrapAnywhere
                    }
                }

                BrandPanel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 320
                    title: "Review"
                    subtitle: catalogAssets.selectedAssetId.length > 0
                              ? ((catalogAssets.selectedIndex + 1) + " / " + catalogAssets.assetCount)
                              : "Arrow keys move, 1-5 rate, P picks, X rejects, U clears."
                    accent: theme.accentCool400

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.space8

                        InfoPill {
                            Layout.fillWidth: true
                            label: "Rating"
                            value: catalogAssets.selectedAssetId.length > 0 ? root.ratingLabel(catalogAssets.selectedRating) : "None"
                            accent: theme.warning
                        }

                        InfoPill {
                            Layout.fillWidth: true
                            label: "Review"
                            value: catalogAssets.selectedAssetId.length > 0 ? root.cullStatusLabel(catalogAssets.selectedCullStatus) : "Waiting"
                            accent: catalogAssets.selectedRejected ? theme.error : (catalogAssets.selectedPicked ? theme.success : theme.accentCool400)
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
                        accent: theme.accentCool400
                        active: appShell.cullingAutoAdvance
                        onClicked: appShell.cullingAutoAdvance = !appShell.cullingAutoAdvance
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        visible: catalogAssets.selectedAssetId.length > 0
                        radius: theme.radius12
                        color: catalogAssets.selectedOnline ? theme.alpha(theme.success, 0.10) : theme.alpha(theme.warning, 0.14)
                        border.width: 1
                        border.color: catalogAssets.selectedOnline ? theme.alpha(theme.success, 0.24) : theme.alpha(theme.warning, 0.26)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.space10
                            spacing: theme.space8

                            Label {
                                Layout.fillWidth: true
                                text: catalogAssets.selectedOnline
                                      ? "Source is available on disk."
                                      : "Source is offline. Recheck mounted drives or relink it to a matching folder."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 11
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
                                    text: "Relink Selected"
                                    enabled: !catalogAssets.selectedOnline
                                    onClicked: root.chooseRelinkFolder(true)
                                }
                            }
                        }
                    }
                }

                BrandPanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: "Projects"
                    subtitle: catalogAssets.projectNames.length > 0
                              ? "Choose a project to focus the gallery or create a new one."
                              : "Create a project to start organizing the gallery."
                    accent: theme.accentWarm400

                    ToneTextField {
                        id: newProjectField
                        Layout.fillWidth: true
                        placeholderText: "Project name"
                        onAccepted: {
                            root.createAndFocusProject(text);
                            text = "";
                        }
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Create Project"
                        tone: "primary"
                        enabled: newProjectField.text.trim().length > 0
                        onClicked: {
                            root.createAndFocusProject(newProjectField.text);
                            newProjectField.text = "";
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: theme.space6
                        boundsBehavior: Flickable.StopAtBounds
                        model: root.projectFilterValues

                        delegate: Rectangle {
                            required property int index
                            required property string modelData

                            width: ListView.view.width
                            height: 50
                            radius: theme.radius10
                            color: catalogAssets.projectFilter === modelData
                                   ? theme.alpha(theme.accentWarm500, 0.18)
                                   : (projectMouseArea.containsMouse ? theme.alpha(theme.ink800, 0.9) : theme.alpha(theme.ink900, 0.76))
                            border.width: 1
                            border.color: catalogAssets.projectFilter === modelData
                                          ? theme.alpha(theme.accentWarm400, 0.74)
                                          : theme.alpha(theme.lineSoft, 0.42)

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: theme.space12
                                spacing: 2

                                Label {
                                    width: parent.width
                                    text: root.projectFilterLabels[index]
                                    color: catalogAssets.projectFilter === modelData ? theme.textStrong : theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    width: parent.width
                                    spacing: theme.space8

                                    Label {
                                        readonly property int assetCount: {
                                            catalogAssets.projectStatsRevision;
                                            return catalogAssets.assetCountForProject(modelData);
                                        }
                                        text: assetCount + (assetCount === 1 ? " asset" : " assets")
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData === "all"
                                              ? "Show the full gallery."
                                              : modelData === "unassigned"
                                                ? "Only assets without a project."
                                                : "Filter the gallery to this project."
                                        color: theme.textFaint
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            MouseArea {
                                id: projectMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.applyProjectSelection(modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
