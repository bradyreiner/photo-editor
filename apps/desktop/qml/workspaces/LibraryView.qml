import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radius12
            color: theme.ink850
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.24)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 188
                    radius: theme.radius12
                    color: theme.alpha(theme.ink900, 0.78)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.2)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 14

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3

                                Label {
                                    text: "Library"
                                    color: "#f3f5f7"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 28
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    text: catalogAssets.assetCount + " visible  •  " + catalogAssets.totalAssetCount + " imported assets  •  " + catalogAssets.driveCount + " drives"
                                    color: "#c1cad2"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 12
                                }

                                Label {
                                    text: root.activeFilterSummary()
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                }
                            }

                            RowLayout {
                                spacing: 8

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
                                    text: catalogAssets.loading ? "Refreshing..." : "Reload"
                                    enabled: !catalogAssets.busy
                                    onClicked: catalogAssets.refresh()
                                }

                                ActionButton {
                                    text: "Clear Filters"
                                    enabled: catalogAssets.searchFilter !== ""
                                             || catalogAssets.importDateFilter !== "all"
                                             || catalogAssets.captureYearFilter !== "all"
                                             || catalogAssets.projectFilter !== "all"
                                             || catalogAssets.mediaTypeFilter !== "all"
                                             || catalogAssets.reviewFilter !== "all"
                                             || catalogAssets.minimumRatingFilter > 0
                                    onClicked: root.clearFilters()
                                }
                            }
                        }

                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: "Search by filename, camera, lens, location..."
                            placeholderTextColor: "#5a6570"
                            color: "#e0e4e8"
                            font.family: "Avenir Next"
                            font.pixelSize: 12
                            text: catalogAssets.searchFilter
                            onTextChanged: catalogAssets.searchFilter = text
                            background: Rectangle {
                                color: "#1a1f25"
                                radius: 6
                                border.color: searchField.activeFocus ? theme.accentWarm400 : "#2a3038"
                                border.width: 1
                            }
                            leftPadding: 10
                            rightPadding: 10
                            topPadding: 6
                            bottomPadding: 6
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: [
                                    {
                                        text: "All " + catalogAssets.assetCount,
                                        value: "all",
                                        accent: theme.accentWarm400
                                    },
                                    {
                                        text: "Picks " + catalogAssets.visiblePickedCount,
                                        value: "picked",
                                        accent: theme.success
                                    },
                                    {
                                        text: "Rejects " + catalogAssets.visibleRejectedCount,
                                        value: "rejected",
                                        accent: theme.error
                                    },
                                    {
                                        text: "Unmarked " + catalogAssets.visibleUnmarkedCount,
                                        value: "unmarked",
                                        accent: theme.accentCool400
                                    }
                                ]

                                delegate: FilterChip {
                                    required property var modelData

                                    text: modelData.text
                                    accent: modelData.accent
                                    active: catalogAssets.reviewFilter === modelData.value
                                    onClicked: catalogAssets.reviewFilter = modelData.value
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Label {
                                text: "Imported"
                                color: "#7f8a94"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 170
                                model: root.importFilterLabels
                                currentIndex: root.indexForValue(root.importFilterValues, catalogAssets.importDateFilter)
                                onActivated: catalogAssets.importDateFilter = root.importFilterValues[currentIndex]
                            }

                            Label {
                                text: "Captured"
                                color: "#7f8a94"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 180
                                model: root.captureFilterLabels
                                currentIndex: root.indexForValue(root.captureFilterValues, catalogAssets.captureYearFilter)
                                onActivated: catalogAssets.captureYearFilter = root.captureFilterValues[currentIndex]
                            }

                            Label {
                                text: "Media"
                                color: "#7f8a94"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 160
                                model: root.mediaFilterLabels
                                currentIndex: root.indexForValue(root.mediaFilterValues, catalogAssets.mediaTypeFilter)
                                onActivated: catalogAssets.mediaTypeFilter = root.mediaFilterValues[currentIndex]
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Label {
                                text: "Review"
                                color: "#7f8a94"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 170
                                model: root.reviewFilterLabels
                                currentIndex: root.indexForValue(root.reviewFilterValues, catalogAssets.reviewFilter)
                                onActivated: catalogAssets.reviewFilter = root.reviewFilterValues[currentIndex]
                            }

                            Label {
                                text: "Source"
                                color: "#7f8a94"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 160
                                model: root.availabilityFilterLabels
                                currentIndex: root.indexForValue(root.availabilityFilterValues, catalogAssets.availabilityFilter)
                                onActivated: catalogAssets.availabilityFilter = root.availabilityFilterValues[currentIndex]
                            }

                            Label {
                                text: "Rating"
                                color: "#7f8a94"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 160
                                model: root.minimumRatingLabels
                                currentIndex: root.indexForValue(root.minimumRatingValues, catalogAssets.minimumRatingFilter)
                                onActivated: catalogAssets.minimumRatingFilter = root.minimumRatingValues[currentIndex]
                            }

                            Item {
                                Layout.fillWidth: true
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
                            spacing: 4

                            property int columns: Math.max(2, Math.floor(width / 250))
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
                                        width: parent.width
                                        readonly property real sourceAspectRatio: previewImage.sourceSize.width > 0 && previewImage.sourceSize.height > 0 ? previewImage.sourceSize.height / previewImage.sourceSize.width : 0.66
                                        height: Math.max(120, width * sourceAspectRatio)
                                        radius: 8
                                        color: "#0f1418"
                                        clip: true

                                        Image {
                                            id: previewImage
                                            anchors.fill: parent
                                            source: thumbnailSource
                                            asynchronous: true
                                            cache: false
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                            sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                                            opacity: online ? 1.0 : 0.48
                                        }

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.margins: 8
                                            radius: 8
                                            color: "#11161bcc"
                                            visible: mediaType !== "photo"
                                            implicitWidth: mediaBadge.implicitWidth + 14
                                            implicitHeight: 22

                                            Label {
                                                id: mediaBadge
                                                anchors.centerIn: parent
                                                text: mediaType === "timeline" ? "Timeline" : "Video"
                                                color: "#f3f5f7"
                                                font.family: "Menlo"
                                                font.pixelSize: 9
                                            }
                                        }

                                        Row {
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: 8
                                            spacing: 6

                                            Rectangle {
                                                visible: rating > 0
                                                radius: 8
                                                color: "#11161bcc"
                                                implicitWidth: ratingBadge.implicitWidth + 14
                                                implicitHeight: 22

                                                Label {
                                                    id: ratingBadge
                                                    anchors.centerIn: parent
                                                    text: "R" + rating
                                                    color: "#f3f5f7"
                                                    font.family: "Menlo"
                                                    font.pixelSize: 9
                                                }
                                            }

                                            Rectangle {
                                                visible: picked || rejected
                                                radius: 8
                                                color: rejected ? "#6a3530dd" : "#334d39dd"
                                                implicitWidth: cullBadge.implicitWidth + 14
                                                implicitHeight: 22

                                                Label {
                                                    id: cullBadge
                                                    anchors.centerIn: parent
                                                    text: rejected ? "Reject" : "Pick"
                                                    color: "#f3f5f7"
                                                    font.family: "Menlo"
                                                    font.pixelSize: 9
                                                }
                                            }

                                            Rectangle {
                                                visible: burstSize > 1
                                                radius: 8
                                                color: "#263445dd"
                                                implicitWidth: burstBadge.implicitWidth + 14
                                                implicitHeight: 22

                                                Label {
                                                    id: burstBadge
                                                    anchors.centerIn: parent
                                                    text: "B " + burstPosition + "/" + burstSize
                                                    color: "#f3f5f7"
                                                    font.family: "Menlo"
                                                    font.pixelSize: 9
                                                }
                                            }

                                            Rectangle {
                                                visible: duplicateGroupSize > 1
                                                radius: 8
                                                color: "#2f4152dd"
                                                implicitWidth: duplicateBadge.implicitWidth + 14
                                                implicitHeight: 22

                                                Label {
                                                    id: duplicateBadge
                                                    anchors.centerIn: parent
                                                    text: "D " + duplicateGroupSize
                                                    color: "#f3f5f7"
                                                    font.family: "Menlo"
                                                    font.pixelSize: 9
                                                }
                                            }

                                            Rectangle {
                                                visible: bestShotSuggested
                                                radius: 8
                                                color: "#6a5331dd"
                                                implicitWidth: suggestionBadge.implicitWidth + 14
                                                implicitHeight: 22

                                                Label {
                                                    id: suggestionBadge
                                                    anchors.centerIn: parent
                                                    text: "Select"
                                                    color: "#f3f5f7"
                                                    font.family: "Menlo"
                                                    font.pixelSize: 9
                                                }
                                            }

                                            Rectangle {
                                                visible: !online
                                                radius: 8
                                                color: "#5d4631dd"
                                                implicitWidth: availabilityBadge.implicitWidth + 14
                                                implicitHeight: 22

                                                Label {
                                                    id: availabilityBadge
                                                    anchors.centerIn: parent
                                                    text: "Offline"
                                                    color: "#f3f5f7"
                                                    font.family: "Menlo"
                                                    font.pixelSize: 9
                                                }
                                            }
                                        }

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 8
                                            radius: 8
                                            color: "#11161bcc"
                                            visible: syncGroupSize > 1
                                            implicitWidth: syncBadge.implicitWidth + 14
                                            implicitHeight: 22

                                            Label {
                                                id: syncBadge
                                                anchors.centerIn: parent
                                                text: "Sync " + syncGroupSize
                                                color: "#f3f5f7"
                                                font.family: "Menlo"
                                                font.pixelSize: 9
                                            }
                                        }

                                        Rectangle {
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            anchors.margins: 8
                                            radius: 8
                                            color: "#11161bcc"
                                            visible: durationText.length > 0
                                            implicitWidth: durationBadge.implicitWidth + 14
                                            implicitHeight: 22

                                            Label {
                                                id: durationBadge
                                                anchors.centerIn: parent
                                                text: durationText
                                                color: "#f3f5f7"
                                                font.family: "Menlo"
                                                font.pixelSize: 9
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
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
                                                      ? "#d0a157"
                                                      : rejected
                                                        ? theme.alpha(theme.error, 0.78)
                                                        : picked
                                                          ? theme.alpha(theme.success, 0.78)
                                                          : theme.alpha(theme.lineSoft, 0.52)
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
                        color: "#14191d"

                        Column {
                            anchors.centerIn: parent
                            spacing: 10

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: catalogAssets.totalAssetCount === 0 ? "No imported media yet." : "No assets match the current filters."
                                color: "#d7dde3"
                                font.family: "Avenir Next"
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: catalogAssets.totalAssetCount === 0 ? "Switch to Import to bring photos and videos in from folders, cards, and drives." : "Try changing the media, imported date, capture date, or project filter."
                                color: "#85909a"
                                font.family: "Avenir Next"
                                font.pixelSize: 12
                            }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: root.importBrowserOpen
                        radius: 16
                        color: Qt.rgba(12 / 255, 15 / 255, 18 / 255, 0.98)
                        border.width: 1
                        border.color: "#29323a"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 14

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Label {
                                        text: "Import From Finder"
                                        color: "#f3f5f7"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 24
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        text: root.localFolderPath.length > 0 ? root.localFolderPath : "Choose a folder, preview the media, then import it."
                                        color: "#b3bdc7"
                                        font.family: "Avenir Next"
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
                                spacing: 8

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
                                    text: catalogAssets.importing ? "Importing..." : "Import This Folder"
                                    tone: "primary"
                                    enabled: root.localFolderPath.length > 0 && !catalogAssets.busy
                                    onClicked: root.importCurrentFolder()
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.localFolderUrl.length > 0 ? 38 : 0
                                visible: root.localFolderUrl.length > 0
                                clip: true
                                orientation: ListView.Horizontal
                                boundsBehavior: Flickable.StopAtBounds
                                spacing: 8
                                model: localFolderModel

                                delegate: Rectangle {
                                    required property int index

                                    width: Math.max(96, folderLabel.implicitWidth + 24)
                                    height: 34
                                    radius: 9
                                    color: "#171c21"

                                    Label {
                                        id: folderLabel
                                        anchors.centerIn: parent
                                        text: localFolderModel.get(index, "fileName")
                                        color: "#d8dde2"
                                        font.family: "Avenir Next"
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
                                    radius: 10
                                    color: "#11161b"
                                    border.width: root.localSelectedFilePath === localImageModel.get(index, "filePath") ? 1 : 0
                                    border.color: "#d0a157"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 6

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 156
                                            radius: 8
                                            color: "#0e1317"
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
                                            color: "#f2f5f7"
                                            font.family: "Avenir Next"
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
            Layout.preferredWidth: 308
            Layout.fillHeight: true
            radius: theme.radius12
            color: theme.ink850
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.24)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Label {
                    text: "Projects"
                    color: "#f2f4f6"
                    font.family: "Avenir Next"
                    font.pixelSize: 18
                    font.weight: Font.Medium
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: cullingColumn.implicitHeight + 20
                    Layout.minimumHeight: Layout.preferredHeight
                    radius: theme.radius8
                    color: theme.alpha(theme.ink900, 0.88)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.34)

                    ColumnLayout {
                        id: cullingColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                text: "Culling"
                                color: "#f2f4f6"
                                font.family: "Avenir Next"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Label {
                                text: catalogAssets.selectedIndex >= 0
                                      ? (catalogAssets.selectedIndex + 1) + " / " + catalogAssets.assetCount
                                      : "No selection"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: catalogAssets.selectedAssetId.length > 0
                                  ? catalogAssets.selectedCameraLabel
                                  : "Select an asset to rate, pick, or reject it."
                            color: "#f2f4f6"
                            font.family: "Avenir Next"
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: catalogAssets.selectedAssetId.length > 0
                                  ? root.cullSummary()
                                  : "Keys: arrows move, 1-5 rate, 0 clears rating, P picks, X rejects, U clears."
                            color: theme.textMuted
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: catalogAssets.selectedBurstSize > 1
                            text: "Burst group  " + catalogAssets.selectedBurstPosition + " / " + catalogAssets.selectedBurstSize
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Visible  " + catalogAssets.assetCount
                                  + "  •  Picks " + catalogAssets.visiblePickedCount
                                  + "  •  Rejects " + catalogAssets.visibleRejectedCount
                                  + "  •  Rated " + catalogAssets.visibleRatedCount
                                  + "  •  Offline " + catalogAssets.visibleOfflineCount
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: catalogAssets.selectedAssetId.length > 0
                            radius: theme.radius8
                            color: catalogAssets.selectedOnline ? theme.alpha(theme.success, 0.12) : theme.alpha(theme.warning, 0.14)
                            border.width: 1
                            border.color: catalogAssets.selectedOnline ? theme.alpha(theme.success, 0.28) : theme.alpha(theme.warning, 0.3)
                            implicitHeight: availabilityColumn.implicitHeight + 20

                            ColumnLayout {
                                id: availabilityColumn
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label {
                                        text: "Source"
                                        color: "#f2f4f6"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        font.capitalization: Font.AllUppercase
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: root.availabilityLabel(catalogAssets.selectedOnline)
                                        color: catalogAssets.selectedOnline ? theme.success : theme.warning
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: catalogAssets.selectedFilePath.length > 0 ? catalogAssets.selectedFilePath : "No file path recorded."
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    wrapMode: Text.WrapAnywhere
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: catalogAssets.selectedOnline
                                          ? "Source is available on disk."
                                          : "Source is offline. Recheck mounted drives or relink it to a matching folder."
                                    color: theme.textMuted
                                    font.family: "Avenir Next"
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

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

                                ActionButton {
                                    Layout.fillWidth: true
                                    compact: true
                                    text: catalogAssets.visibleOfflineCount > 0
                                          ? "Relink " + catalogAssets.visibleOfflineCount + " Offline Assets"
                                          : "Relink Offline Assets"
                                    enabled: catalogAssets.visibleOfflineCount > 0
                                    onClicked: root.chooseRelinkFolder(false)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

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
                            spacing: 6

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

                        ActionButton {
                            Layout.fillWidth: true
                            compact: true
                            text: root.focusCheckEnabled ? "Focus Check On" : "Focus Check Off"
                            accent: theme.warning
                            active: root.focusCheckEnabled
                            onClicked: root.focusCheckEnabled = !root.focusCheckEnabled
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: catalogAssets.projectNames.length > 0 ? "Choose a project from the list to filter the gallery." : "Create a project to start organizing the gallery."
                    color: "#9ca6af"
                    font.family: "Avenir Next"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    id: newProjectCard
                    Layout.fillWidth: true
                    Layout.preferredHeight: newProjectColumn.implicitHeight + 20
                    Layout.minimumHeight: Layout.preferredHeight
                    radius: theme.radius8
                    color: theme.alpha(theme.ink900, 0.88)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.34)

                    ColumnLayout {
                        id: newProjectColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Label {
                            text: "New Project"
                            color: "#f2f4f6"
                            font.family: "Avenir Next"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                        }

                        Label {
                            text: "Create a project, then click it in the list to make it the active gallery view."
                            color: "#9ca6af"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

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
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius8
                    color: theme.alpha(theme.ink900, 0.88)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.34)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                text: "Browse Projects"
                                color: "#f2f4f6"
                                font.family: "Avenir Next"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Label {
                                text: catalogAssets.projectNames.length + (catalogAssets.projectNames.length === 1 ? " project" : " projects")
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: theme.alpha(theme.lineSoft, 0.48)
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            boundsBehavior: Flickable.StopAtBounds
                            model: root.projectFilterValues

                            delegate: Rectangle {
                                required property int index
                                required property string modelData

                                width: ListView.view.width
                                height: 48
                                radius: theme.radius8
                                color: catalogAssets.projectFilter === modelData ? theme.alpha(theme.accentWarm500, 0.22) : (projectMouseArea.containsMouse ? theme.alpha(theme.ink800, 0.9) : theme.alpha(theme.ink900, 0.72))
                                border.width: 1
                                border.color: catalogAssets.projectFilter === modelData ? theme.alpha(theme.accentWarm400, 0.8) : theme.alpha(theme.lineSoft, 0.46)

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.margins: 12
                                    spacing: 2

                                    Label {
                                        width: parent.width
                                        text: root.projectFilterLabels[index]
                                        color: catalogAssets.projectFilter === modelData ? "#f4ede2" : "#f2f4f6"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }

                                    RowLayout {
                                        width: parent.width
                                        spacing: 8

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
                                            text: modelData === "all" ? "Show the full gallery." : modelData === "unassigned" ? "Only assets without a project." : "Filter the gallery to this project."
                                            color: theme.textFaint
                                            font.family: "Avenir Next"
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

                        Rectangle {
                            Layout.fillWidth: true
                            visible: catalogAssets.selectedAssetId.length > 0
                            radius: theme.radius8
                            color: theme.alpha(theme.accentCool400, 0.08)
                            border.width: 1
                            border.color: theme.alpha(theme.accentCool400, 0.2)
                            implicitHeight: intelligenceColumn.implicitHeight + 20

                            ColumnLayout {
                                id: intelligenceColumn
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label {
                                        text: "Intelligence"
                                        color: "#f2f4f6"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        font.capitalization: Font.AllUppercase
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: catalogAssets.selectedBestShotSuggested ? "Suggested Select" : "Cluster Context"
                                        color: theme.accentCool300
                                        font.family: theme.fontMono
                                        font.pixelSize: 10
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: catalogAssets.selectedBurstSize > 1
                                          ? "Burst group " + catalogAssets.selectedBurstPosition + " / " + catalogAssets.selectedBurstSize + " with a suggested keeper."
                                          : catalogAssets.selectedDuplicateGroupSize > 1
                                            ? "Duplicate cluster of " + catalogAssets.selectedDuplicateGroupSize + " related frames."
                                            : "No duplicate or burst cluster detected for this frame."
                                    color: theme.textMuted
                                    font.family: "Avenir Next"
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: catalogAssets.selectedSyncGroupSize > 1
                                          ? "Suggested sync group: " + catalogAssets.selectedSyncGroupSize + " frames with the same camera, lens, and day."
                                          : "No sync cohort suggestion yet for this frame."
                                    color: theme.textMuted
                                    font.family: "Avenir Next"
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
