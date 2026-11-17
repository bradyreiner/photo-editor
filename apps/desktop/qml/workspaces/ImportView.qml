pragma ComponentBehavior: Bound
import QtQuick
import QtCore
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.folderlistmodel
import "../components"

Rectangle {
    id: root
    objectName: "importWorkspace"
    color: "transparent"

    signal reviewImportedRequested()

    Theme { id: theme }
    Settings {
        id: importBrowserSettings
        category: "ImportBrowser"
        property string recentSourcesJson: "[]"
        property string favoriteSourcesJson: "[]"
        property bool groupRawJpeg: true
        property string dismissedReceiptId: ""
    }
    property string sourceBrowserMode: "locations"
    readonly property var savedRecentSources: parseSavedSources(importBrowserSettings.recentSourcesJson)
    readonly property var savedFavoriteSources: parseSavedSources(importBrowserSettings.favoriteSourcesJson)
    readonly property var sourceBrowserEntries: {
        if (sourceBrowserMode === "locations") return catalogAssets.importSources
        const paths = sourceBrowserMode === "favorites" ? savedFavoriteSources : savedRecentSources
        return paths.map(function(path) { return {path: path, url: root.fileUrlForPath(path), label: root.fileNameForPath(path) || "Computer", subtitle: ""} })
    }
    property string dismissedImportJobId: importBrowserSettings.dismissedReceiptId
    onDismissedImportJobIdChanged: { importBrowserSettings.dismissedReceiptId = dismissedImportJobId; importBrowserSettings.sync() }
    property real previewZoom: 0
    property int duplicateReviewIndex: -1
    readonly property var duplicateReviewMatches: importPreflight.duplicateMatches || []
    readonly property var duplicateReviewMatch: duplicateReviewMatches[duplicateReviewIndex] || ({})

    readonly property string importVisibilityContract: "import-visibility:v3|source:source-rail-media-grid-empty|destination:library-backup-when-enabled|review:photo-preview-compact-choices-readiness-issues|progress:import-job-summary|actions:one-persistent-selected-import|advanced:workflow-disclosure-filename-duplicate-backup-policies|preserve:functionality"
    readonly property string importSurfaceContract: "import-surfaces:v3|source:left-rail-and-media-grid|destination:library-storage-disclosure|review:photo-choices-copy-readiness-details|advanced:workflow-preset-organization-pair-policy|actions:one-primary-import-selected-count|no-elide:true|no-overlap:true"
    readonly property string importWorkflowPolishContract: "import-workflow-polish:v3|flow:choose-source-select-photos-destination-review-import|review-first-viewport:photo-collection-copy-ready-blockers|advanced:progressive-disclosure-filename-duplicate-backup|interaction:choose-source-select-visible-clear-review-confirm-retry-refresh|primary:persistent-import-selected-count-accessible-disabled-reason|text:no-elide-no-overlap-paths-wrap|functionality:preserved"
    readonly property string importPageCompletionContract: "page-completion:v3|import|source:rail-folder-navigation|content:media-grid-filter-selection-empty|destination:library-storage-backup-when-enabled|review:photo-target-copy-readiness-issues|actions:one-persistent-import-selected-count-confirm-cancel|states:no-source-empty-filtered-selected-importing-failed-complete"
    readonly property string importSafetyContract: "import-safety:v1|preflight:source-destination-mode-count-types-size|problems:duplicates-unsupported-unreadable-raw-jpeg|organization:folders-filenames|modes:managed-copy-referenced-link|duplicate-policy:skip-replace|progress:nonblocking|summary:success-skipped-duplicates-failures|retry:job-queue"

    property string sourceRootPath: ""
    property string sourceRootUrl: ""
    property string sourceLabel: ""
    property bool sourceRootIsFileSelection: false
    property var stagedPickerFilePaths: []
    property string pendingFolderPath: ""
    property string pendingFolderUrl: ""
    property string currentFolderPath: ""
    property string currentFolderUrl: ""
    property string projectTarget: ""
    property string workflowProfile: appShell.importWorkflowProfile
    property string organizationTemplate: "date-camera"
    property string rawJpegPolicy: "both"
    property string importPresetStatus: ""
    property bool advancedImportOptionsOpen: false
    property bool newProjectEntryOpen: false
    property bool importProjectChooserOpen: false
    property string importMode: "managed"
    property string duplicatePolicy: "skip"
    property string pendingImportKind: "selection"
    property string pendingImportRootPath: ""
    property var pendingImportPaths: []
    readonly property var importPreflight: catalogAssets.importPreflightResult
    property string mediaQuickFilter: "all"
    property string filenameQuery: ""
    property real thumbnailSize: 224
    readonly property bool compactSourceControls: height < theme.scaled(550)
    readonly property bool compactImportDensity: theme.interfaceScale >= 1.5
    property string selectionAnchorPath: ""
    property string dropMessage: ""
    property bool dropMessageIsError: false
    property bool reviewPathsExpanded: false
    property bool reviewSelectionExpanded: false
    property string copiedReviewPath: ""
    property var previewPaths: []
    property int previewIndex: -1
    readonly property string previewCurrentPath: previewPaths[previewIndex] || ""
    readonly property bool previewCurrentSelected: {
        selectionRevision
        return !!selectedPathsByFile[previewCurrentPath]
    }
    property var selectedPathsByFile: ({})
    property int selectionRevision: 0
    readonly property var activeImportJob: {
        if (!jobQueue)
            return ({})
        jobQueue.jobCount
        jobQueue.status
        return jobQueue.latestJobForKind("catalog_import")
    }

    readonly property bool hasSource: sourceRootPath.length > 0
    readonly property bool canGoUp: currentFolderPath.length > 0 && currentFolderPath !== sourceRootPath
    readonly property bool hasStagedSelection: selectedFileCount > 0
    readonly property bool canImportSelection: hasStagedSelection && !catalogAssets.busy
    readonly property bool canImportFolder: currentFolderPath.length > 0 && !sourceRootIsFileSelection && !catalogAssets.busy
    readonly property bool canConfirmImport: !!importPreflight.ok && !catalogAssets.importPreflightBusy && !catalogAssets.busy && !appShell.photoLibraryMigrationRunning
    readonly property bool canActivatePersistentImport: importProjectChooserOpen
                                                        ? canConfirmImport
                                                        : canImportSelection && !appShell.photoLibraryMigrationRunning
    readonly property string importActionContract: "import-actions:v3|split-button:false|primary:persistent-import-selected-count-review-then-confirm|menu:import-folder,import-with-preset,select-visible,clear-selection,refresh-sources|disabled-reasons:tooltip-and-accessibility|cancel-and-recovery:preserved"
    readonly property string currentFolderName: currentFolderPath.length > 0 ? fileNameForPath(currentFolderPath) : "No folder"
    readonly property int visiblePhotoCount: {
        const total = sourceRootIsFileSelection ? stagedPickerFilePaths.length : fileModel.count
        let count = 0
        for (let index = 0; index < total; ++index) {
            const filePath = sourceRootIsFileSelection
                             ? stagedPickerFilePaths[index]
                             : fileModel.get(index, "filePath")
            if (!isVideoFile(filePath))
                count += 1
        }
        return count
    }
    readonly property int visibleVideoCount: {
        const total = sourceRootIsFileSelection ? stagedPickerFilePaths.length : fileModel.count
        let count = 0
        for (let index = 0; index < total; ++index) {
            const filePath = sourceRootIsFileSelection
                             ? stagedPickerFilePaths[index]
                             : fileModel.get(index, "filePath")
            if (isVideoFile(filePath))
                count += 1
        }
        return count
    }
    readonly property var filteredFileEntries: {
        const filterMode = mediaQuickFilter
        const pickerSelection = sourceRootIsFileSelection ? stagedPickerFilePaths : null
        const total = pickerSelection ? pickerSelection.length : fileModel.count
        let entries = []
        for (let index = 0; index < total; ++index) {
            const filePath = pickerSelection ? pickerSelection[index] : fileModel.get(index, "filePath")
            const isVideo = root.isVideoFile(filePath)
            if (filterMode === "photos" && isVideo)
                continue
            if (filterMode === "videos" && !isVideo)
                continue
            if (filterMode === "selected" && !selectedPathsByFile[filePath])
                continue

            const fileName = pickerSelection ? root.fileNameForPath(filePath) : fileModel.get(index, "fileName")
            if (filenameQuery.trim().length > 0 && fileName.toLowerCase().indexOf(filenameQuery.trim().toLowerCase()) < 0)
                continue
            entries.push({
                           filePath: filePath,
                           filePaths: [filePath],
                           fileName: fileName,
                           fileUrl: pickerSelection ? root.fileUrlForPath(filePath) : fileModel.get(index, "fileUrl"),
                           isVideo: isVideo,
                           extension: fileName.indexOf(".") >= 0
                                      ? fileName.split(".").slice(-1)[0].toUpperCase()
                                      : "FILE"
                       })
        }
        if (!importBrowserSettings.groupRawJpeg) return entries
        const pairs = ({})
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i]
            const ext = entry.extension.toLowerCase()
            const raw = /^(3fr|arw|cr2|cr3|dng|erf|iiq|kdc|kdc|mef|mos|nef|nrw|orf|pef|raf|raw|rw2|sr2|srf|x3f)$/.test(ext)
            const jpeg = ext === "jpg" || ext === "jpeg"
            if (!raw && !jpeg) continue
            const key = root.parentPathForFile(entry.filePath) + "/" + entry.fileName.replace(/\.[^.]+$/, "").toLowerCase()
            if (!pairs[key]) pairs[key] = {raw: [], jpeg: []}
            pairs[key][raw ? "raw" : "jpeg"].push(entry)
        }
        const consumed = ({})
        return entries.filter(function(entry) {
            if (consumed[entry.filePath]) return false
            const key = root.parentPathForFile(entry.filePath) + "/" + entry.fileName.replace(/\.[^.]+$/, "").toLowerCase()
            const pair = pairs[key]
            if (!pair || pair.raw.length === 0 || pair.jpeg.length === 0) return true
            const members = pair.jpeg.concat(pair.raw)
            const representative = pair.jpeg[0]
            const memberPaths = members.map(function(member) { consumed[member.filePath] = true; return member.filePath })
            entry.filePath = representative.filePath
            entry.fileUrl = representative.fileUrl
            entry.fileName = representative.fileName.replace(/\.[^.]+$/, "")
            entry.extension = "RAW + JPEG"
            entry.filePaths = memberPaths
            return true
        })
    }
    readonly property int filteredFileCount: filteredFileEntries.length
    readonly property var selectedFilePaths: {
        selectionRevision
        const paths = []
        for (let filePath in selectedPathsByFile) {
            if (selectedPathsByFile[filePath])
                paths.push(filePath)
        }
        paths.sort()
        return paths
    }
    readonly property int selectedFileCount: selectedFilePaths.length
    readonly property int importPreflightBlockerCount: {
        const issues = importPreflight.issues || []
        let count = 0
        for (let index = 0; index < issues.length; ++index) {
            if (issues[index].severity === "error")
                ++count
        }
        return count
    }

    function preflightExceptionsText() {
        const parts = []
        if (importPreflightBlockerCount > 0)
            parts.push(importPreflightBlockerCount + " blockers")
        if ((importPreflight.duplicateCount || 0) > 0)
            parts.push(importPreflight.duplicateCount + " duplicates")
        if ((importPreflight.unsupportedCount || 0) > 0)
            parts.push(importPreflight.unsupportedCount + " unsupported")
        if ((importPreflight.unreadableCount || 0) > 0)
            parts.push(importPreflight.unreadableCount + " unreadable")
        return parts.join(" · ")
    }

    component MediaFilterTab: NavigationTab {
        underlineSelection: true
        implicitHeight: theme.scaled(36)
        Accessible.description: selected ? "Current media filter" : "Filter media"
    }
    component ImportDialogButton: ActionButton {
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
    }
    component ReviewLocation: ColumnLayout {
        id: location
        property string caption: ""
        property string path: ""
        property string summary: ""
        property bool showPath: root.reviewPathsExpanded
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: theme.space4

        Label {
            text: location.caption
            color: theme.textFaint
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8
            Label {
                Layout.fillWidth: true
                id: locationName
                Accessible.description: location.path
                HoverHandler { id: locationHover }
                AppToolTip { targetItem: locationName; visible: locationHover.hovered; text: location.path }
                text: location.summary.length > 0 ? location.summary
                      : root.fileNameForPath(location.path.replace(/\/+$/, "")) || location.path || "Checking…"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: Font.Medium
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
            ActionButton {
                objectName: "importCopy" + location.caption + "PathButton"
                visible: location.path.length > 0
                iconName: root.copiedReviewPath === location.path ? "check" : "copy"
                showText: false
                compact: true
                embedded: true
                tooltipText: root.copiedReviewPath === location.path ? "Path copied" : "Copy " + location.caption.toLowerCase() + " path"
                onClicked: root.copyReviewPath(location.path)
            }
        }

        Label {
            Layout.fillWidth: true
            visible: location.showPath && location.path.length > 0 && location.summary !== location.path
            text: location.path
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
            wrapMode: Text.WrapAnywhere
        }
    }

    readonly property int selectedPhotoCount: {
        selectionRevision
        let count = 0
        for (let index = 0; index < selectedFilePaths.length; ++index) {
            if (!isVideoFile(selectedFilePaths[index]))
                ++count
        }
        return count
    }
    readonly property int selectedVideoCount: {
        selectionRevision
        let count = 0
        for (let index = 0; index < selectedFilePaths.length; ++index) {
            if (isVideoFile(selectedFilePaths[index]))
                ++count
        }
        return count
    }
    readonly property int selectedFolderCount: {
        selectionRevision
        const folders = ({})
        for (let index = 0; index < selectedFilePaths.length; ++index)
            folders[parentPathForFile(selectedFilePaths[index])] = true
        return Object.keys(folders).length
    }
    readonly property var projectOptionValues: {
        const values = [""]
        for (let i = 0; i < catalogAssets.projectNames.length; ++i)
            values.push(catalogAssets.projectNames[i])
        return values
    }
    readonly property var projectOptionLabels: {
        const labels = ["Photo Library"]
        for (let i = 0; i < catalogAssets.projectNames.length; ++i)
            labels.push(catalogAssets.projectNames[i])
        return labels
    }

    function fileNameForPath(filePath) {
        const parts = filePath.split("/")
        return parts.length > 0 ? parts[parts.length - 1] : filePath
    }

    function fileUrlForPath(filePath) {
        const normalized = normalizedImportPath(filePath)
        if (!normalized.startsWith("/"))
            return ""
        const escapedPath = normalized.split("/").map(function(part) {
            return encodeURIComponent(part)
        }).join("/")
        return "file://" + escapedPath
    }

    function pickerFolderLabel(filePath) {
        const parent = parentPathForFile(filePath)
        const rootPath = sourceRootPath.endsWith("/") ? sourceRootPath : sourceRootPath + "/"
        if (parent.indexOf(rootPath) === 0)
            return parent.slice(rootPath.length)
        return fileNameForPath(parent)
    }

    function isVideoFile(filePath) {
        const lower = filePath.toLowerCase()
        return lower.endsWith(".mp4")
                || lower.endsWith(".mov")
                || lower.endsWith(".m4v")
                || lower.endsWith(".mpeg")
                || lower.endsWith(".mpg")
                || lower.endsWith(".webm")
    }

    function parentPathForFile(filePath) {
        const lastSlash = filePath.lastIndexOf("/")
        if (lastSlash < 0)
            return ""
        if (lastSlash === 0)
            return "/"
        return filePath.slice(0, lastSlash)
    }

    function normalizedImportPath(filePath) {
        const path = String(filePath).replace(/\\/g, "/")
        const absolute = path.startsWith("/")
        const parts = path.split("/")
        const normalized = []
        for (let index = 0; index < parts.length; ++index) {
            const part = parts[index]
            if (part.length === 0 || part === ".")
                continue
            if (part === "..") {
                if (normalized.length > 0 && normalized[normalized.length - 1] !== "..")
                    normalized.pop()
                else if (!absolute)
                    normalized.push(part)
                continue
            }
            normalized.push(part)
        }
        const joined = normalized.join("/")
        return absolute ? "/" + joined : joined
    }

    function commonDirectoryForFiles(filePaths) {
        if (!filePaths || filePaths.length === 0)
            return ""

        const directories = []
        for (let index = 0; index < filePaths.length; ++index) {
            const normalized = normalizedImportPath(filePaths[index])
            if (!normalized.startsWith("/"))
                return ""
            const directory = parentPathForFile(normalized)
            if (directory.length === 0)
                return ""
            directories.push(directory)
        }

        const firstParts = directories[0].split("/").filter(function(part) { return part.length > 0 })
        let commonLength = firstParts.length
        for (let index = 1; index < directories.length && commonLength > 0; ++index) {
            const parts = directories[index].split("/").filter(function(part) { return part.length > 0 })
            commonLength = Math.min(commonLength, parts.length)
            let shared = 0
            while (shared < commonLength && firstParts[shared] === parts[shared])
                ++shared
            commonLength = shared
        }
        return commonLength === 0 ? "/" : "/" + firstParts.slice(0, commonLength).join("/")
    }

    function stagePickedFiles(filePaths) {
        if (!filePaths || filePaths.length === 0)
            return false

        const normalizedPaths = []
        const seenPaths = ({})
        for (let index = 0; index < filePaths.length; ++index) {
            const filePath = normalizedImportPath(filePaths[index])
            if (!filePath.startsWith("/") || seenPaths[filePath])
                continue
            seenPaths[filePath] = true
            normalizedPaths.push(filePath)
        }
        if (normalizedPaths.length === 0)
            return false

        const folderPath = commonDirectoryForFiles(normalizedPaths)
        if (folderPath.length === 0)
            return false
        // Construct the directory URL in the native path layer so spaces and
        // other escaped characters remain valid when files span directories.
        const folderUrl = catalogAssets.parentFolderUrl((folderPath === "/" ? "" : folderPath) + "/.looksy-import-selection")
        if (folderUrl.length === 0)
            return false

        const parents = ({})
        for (let index = 0; index < normalizedPaths.length; ++index)
            parents[parentPathForFile(normalizedPaths[index])] = true
        const sourceName = folderPath === "/" ? "Computer" : fileNameForPath(folderPath)
        openSource(folderPath, folderUrl, Object.keys(parents).length > 1 ? "Multiple folders" : sourceName)
        sourceRootIsFileSelection = true

        const nextSelection = ({})
        for (let index = 0; index < normalizedPaths.length; ++index)
            nextSelection[normalizedPaths[index]] = true
        selectedPathsByFile = nextSelection
        stagedPickerFilePaths = normalizedPaths
        mediaQuickFilter = "all"
        selectionRevision += 1
        return true
    }

    function clearSelection() {
        selectedPathsByFile = ({})
        importProjectChooserOpen = false
        selectionRevision += 1
        selectionAnchorPath = ""
    }

    function entryPaths(entry) { return entry.filePaths || [entry.filePath] }
    function parseSavedSources(json) {
        try {
            const paths = JSON.parse(json)
            return Array.isArray(paths) ? paths.filter(function(path) { return typeof path === "string" && path.startsWith("/") }).slice(0, 8) : []
        } catch (error) { return [] }
    }
    function rememberSource(path) {
        const recent = savedRecentSources.filter(function(value) { return value !== path })
        recent.unshift(path)
        importBrowserSettings.recentSourcesJson = JSON.stringify(recent.slice(0, 8))
    }
    function toggleSourceFavorite() {
        if (!hasSource) return
        const favorites = savedFavoriteSources.slice()
        const index = favorites.indexOf(sourceRootPath)
        if (index >= 0) favorites.splice(index, 1)
        else favorites.unshift(sourceRootPath)
        importBrowserSettings.favoriteSourcesJson = JSON.stringify(favorites.slice(0, 8))
    }
    function openSavedSource(path) {
        const entry = catalogAssets.inspectImportDrop([fileUrlForPath(path)])
        if (!entry.folderPath) {
            dropMessage = "This saved source is unavailable. Connect it and try again."
            dropMessageIsError = true
            return false
        }
        openSource(entry.folderPath, entry.folderUrl, fileNameForPath(entry.folderPath))
        return true
    }
    function chooseImportStorage(backup) {
        const path = catalogAssets.pickFolder(backup ? "Choose an originals backup folder" : "Choose a Photo Library folder")
        if (!path) return false
        const accepted = backup ? appShell.setOriginalsBackupLocation(path) : appShell.setPhotoLibraryLocation(path)
        storageFeedback = appShell.storageConfigurationStatus
        return accepted
    }
    function setPreviewZoom(value) {
        previewZoom = value === 0 ? 0 : Math.max(0.1, Math.min(4, value))
        Qt.callLater(function() {
            previewFlick.contentX = Math.max(0, (previewFlick.contentWidth - previewFlick.width) / 2)
            previewFlick.contentY = Math.max(0, (previewFlick.contentHeight - previewFlick.height) / 2)
        })
    }
    property string storageFeedback: ""
    readonly property var previewFileDetails: catalogAssets.importFileDetails.path === previewCurrentPath ? catalogAssets.importFileDetails : ({loading: previewCurrentPath.length > 0})
    onPreviewCurrentPathChanged: {
        previewZoom = 0
        metadataDelay.restart()
    }
    Timer {
        id: metadataDelay
        interval: 80
        onTriggered: catalogAssets.requestImportFileDetails(root.previewCurrentPath)
    }

    function selectVisibleEntry(index, modifiers) {
        const entries = filteredFileEntries.slice()
        if (index < 0 || index >= entries.length)
            return
        const path = entries[index].filePath
        const anchor = entries.findIndex(function(entry) { return entry.filePath === root.selectionAnchorPath })
        if ((modifiers & Qt.ShiftModifier) && anchor >= 0) {
            const next = Object.assign({}, selectedPathsByFile)
            for (let i = Math.min(anchor, index); i <= Math.max(anchor, index); ++i) {
                const members = entryPaths(entries[i])
                for (let j = 0; j < members.length; ++j) next[members[j]] = true
            }
            selectedPathsByFile = next
            selectionRevision += 1
        } else {
            const next = Object.assign({}, selectedPathsByFile)
            const members = entryPaths(entries[index])
            const allSelected = members.every(function(member) { return !!next[member] })
            for (let j = 0; j < members.length; ++j) {
                if (allSelected) delete next[members[j]]
                else next[members[j]] = true
            }
            selectedPathsByFile = next
            selectionRevision += 1
            selectionAnchorPath = path
        }
        mediaGrid.currentIndex = Math.min(index, filteredFileCount - 1)
    }

    function openPreview(index) {
        if (index < 0 || index >= filteredFileEntries.length)
            return
        const paths = []
        let start = 0
        for (let i = 0; i < filteredFileEntries.length; ++i) {
            if (i === index) start = paths.length
            const members = entryPaths(filteredFileEntries[i])
            for (let j = 0; j < members.length; ++j) paths.push(members[j])
        }
        previewPaths = paths
        previewIndex = start
        mediaPreviewDialog.open()
    }

    function openReviewPreview(path) {
        previewPaths = pendingImportPaths.slice()
        previewIndex = previewPaths.indexOf(path)
        if (previewIndex >= 0)
            mediaPreviewDialog.open()
    }

    function stageDroppedUrls(urls) {
        if (catalogAssets.busy || importProjectChooserOpen)
            return false
        const result = catalogAssets.inspectImportDrop(urls)
        if (result.error) {
            dropMessage = result.error
            dropMessageIsError = true
            return false
        }
        if (result.folderPath) {
            openSource(result.folderPath, result.folderUrl, fileNameForPath(result.folderPath))
            dropMessage = "Folder opened. Select files to review, or review the entire folder from Import options."
        } else {
            if (!stagePickedFiles(result.files || []))
                return false
            dropMessage = result.files.length + " files ready to review" +
                          (result.ignoredCount > 0 ? " · " + result.ignoredCount + " unsupported or unavailable items ignored" : "")
        }
        dropMessageIsError = false
        return true
    }

    function reviewActionText() {
        return selectedFileCount > 0 ? "Review " + selectedFileCount + (selectedFileCount === 1 ? " file" : " files") : "Review import"
    }

    function confirmActionText() {
        if (catalogAssets.importPreflightBusy)
            return "Checking files…"
        const count = importPreflight.importableCount || 0
        const kind = count > 0 && importPreflight.photoCount === count ? (count === 1 ? " photo" : " photos")
                   : count > 0 && importPreflight.videoCount === count ? (count === 1 ? " video" : " videos")
                   : (count === 1 ? " file" : " files")
        return count > 0 ? "Import " + count + kind + (importPreflight.backupEnabled ? " with Backup" : "") : "Import"
    }

    function persistentImportActionText() {
        if (importProjectChooserOpen && pendingImportKind === "folder")
            return catalogAssets.importPreflightBusy ? "Checking folder…" : confirmActionText()
        const count = importProjectChooserOpen ? pendingImportPaths.length : selectedFileCount
        return "Import selected photos" + (count > 0 ? " · " + count : "")
    }

    function importAdvancedPolicySummaryText() {
        const organization = organizationTemplate === "date-camera" ? "Date + Camera folders" : "Source folders"
        const duplicates = duplicatePolicy === "skip" ? "skip matching files" : "replace matching files"
        const backup = importMode !== "managed" ? "backup not used for Leave in Place"
                      : appShell.originalsBackupEnabled ? "verified originals backup on" : "originals backup off"
        return organization + " · original filenames · duplicates " + duplicates + " · " + backup
    }

    function persistentImportDisabledReason() {
        if (catalogAssets.busy)
            return "An import operation is already running."
        if (appShell.photoLibraryMigrationRunning)
            return appShell.storageConfigurationStatus || "Wait for Photo Library migration to finish."
        if (importProjectChooserOpen) {
            if (catalogAssets.importPreflightBusy)
                return "Checking the selected files and destination."
            if (!importPreflight.ok) {
                const issues = importPreflight.issues || []
                for (let index = 0; index < issues.length; ++index) {
                    if (issues[index].severity === "error")
                        return String(issues[index].detail || issues[index].title || "Resolve the import review issue before importing.")
                }
                return "No files are ready to import. Review the destination and file handling options."
            }
            return ""
        }
        if (selectedFileCount === 0)
            return "Select photos or videos to import."
        return ""
    }

    function activatePersistentImportAction() {
        if (!canActivatePersistentImport)
            return
        if (importProjectChooserOpen)
            confirmSelectedImport()
        else
            importSelected()
    }

    function copyReviewPath(path) {
        if (appShell.copyTextToClipboard(path))
            copiedReviewPath = path
    }

    function toggleSelection(filePath) {
        const nextSelection = Object.assign({}, selectedPathsByFile)
        if (nextSelection[filePath])
            delete nextSelection[filePath]
        else
            nextSelection[filePath] = true
        selectedPathsByFile = nextSelection
        selectionRevision += 1
    }

    function selectAllVisible() {
        const nextSelection = Object.assign({}, selectedPathsByFile)
        for (let index = 0; index < filteredFileEntries.length; ++index) {
            const members = entryPaths(filteredFileEntries[index])
            for (let j = 0; j < members.length; ++j) nextSelection[members[j]] = true
        }
        selectedPathsByFile = nextSelection
        selectionRevision += 1
    }

    function canSelectAll() {
        return filteredFileCount > 0
    }

    function selectAll() {
        if (!canSelectAll())
            return
        selectAllVisible()
    }

    function canDeselect() {
        return selectedFileCount > 0 || importProjectChooserOpen
    }

    function deselectAll() {
        if (!canDeselect())
            return
        clearSelection()
    }

    function mediaFilterLabel() {
        if (mediaQuickFilter === "photos")
            return "Photos"
        if (mediaQuickFilter === "videos")
            return "Videos"
        if (mediaQuickFilter === "selected")
            return "Selected"
        return "All Media"
    }

    function supportedImportTypesText() {
        return "RAW cameras (CR2/CR3, NEF, ARW, DNG, RAF), JPG, PNG, TIFF, HEIC, AVIF, WebP, MP4, MOV, and more."
    }

    function sourceConfirmationText() {
        if (!root.hasSource)
            return "Choose Source"
        if (sourceRootIsFileSelection)
            return "Files Ready"
        if (root.currentFolderPath.length === 0)
            return "Source Ready"
        return "Folder Ready"
    }

    function selectionMixText() {
        if (selectedFileCount === 0)
            return String(filteredFileCount) + " visible"
        if (filteredFileEntries.some(function(entry) { return entry.filePaths && entry.filePaths.length > 1 }))
            return selectedFileCount + (selectedFileCount === 1 ? " file" : " files")
        let mediaSummary = ""
        if (selectedPhotoCount > 0 && selectedVideoCount > 0)
            mediaSummary = selectedPhotoCount + " photo / " + selectedVideoCount + " video"
        else if (selectedVideoCount > 0)
            mediaSummary = selectedVideoCount + (selectedVideoCount === 1 ? " video" : " videos")
        else
            mediaSummary = selectedPhotoCount + (selectedPhotoCount === 1 ? " photo" : " photos")
        return selectedFolderCount > 1 ? mediaSummary + " · " + selectedFolderCount + " folders" : mediaSummary
    }

    function syncProjectTarget() {
        if (projectTarget.length > 0 && catalogAssets.projectNames.indexOf(projectTarget) !== -1)
            return
        projectTarget = ""
    }

    function createProjectAndSelect(projectName) {
        const trimmedName = projectName.trim()
        if (trimmedName.length === 0)
            return
        projectTarget = trimmedName
        catalogAssets.createProject(trimmedName)
    }

    function openSource(path, url, label) {
        rememberSource(path)
        filenameQuery = ""
        selectionAnchorPath = ""
        dropMessage = ""
        mediaPreviewDialog.close()
        sourceRootPath = path
        sourceRootUrl = url
        sourceLabel = label
        sourceRootIsFileSelection = false
        stagedPickerFilePaths = []
        currentFolderPath = ""
        currentFolderUrl = ""
        pendingFolderPath = path
        pendingFolderUrl = url
        deferredFolderLoadTimer.restart()
        importProjectChooserOpen = false
        clearSelection()
    }

    function browseForSource() {
        const folderUrl = catalogAssets.pickFolder("Choose an import source")
        if (!folderUrl || folderUrl.length === 0)
            return

        const folderPath = catalogAssets.localPathForUrl(folderUrl)
        if (folderPath.length === 0)
            return

        openSource(folderPath, folderUrl, fileNameForPath(folderPath))
    }

    function browseForFiles() {
        const filePaths = catalogAssets.pickFiles("Choose one or more media files")
        stagePickedFiles(filePaths)
    }

    function ensureDefaultSource() {
        if (sourceRootPath.length > 0)
            return

        if (catalogAssets.importSources.length === 0)
            return

        let preferred = catalogAssets.importSources[0]
        for (let index = 0; index < catalogAssets.importSources.length; ++index) {
            const candidate = catalogAssets.importSources[index]
            if (candidate.label === "Pictures") {
                preferred = candidate
                break
            }
        }

        openSource(preferred.path, preferred.url, preferred.label)
    }

    function openFolder(folderUrl) {
        const folderPath = catalogAssets.localPathForUrl(folderUrl)
        if (folderPath.length === 0)
            return

        if (sourceRootPath.length > 0) {
            const normalizedRoot = sourceRootPath.endsWith("/") ? sourceRootPath : sourceRootPath + "/"
            if (folderPath !== sourceRootPath && folderPath.indexOf(normalizedRoot) !== 0)
                return
        }

        currentFolderUrl = folderUrl
        currentFolderPath = folderPath
    }

    function goUp() {
        if (!canGoUp)
            return
        openFolder(catalogAssets.parentFolderUrl(currentFolderUrl))
    }

    function importSelected() {
        if (sourceRootPath.length === 0 || selectedFileCount === 0)
            return
        pendingImportKind = "selection"
        reviewPathsExpanded = false
        copiedReviewPath = ""
        pendingImportRootPath = sourceRootPath
        pendingImportPaths = selectedFilePaths
        importProjectChooserOpen = true
        refreshImportPreflight()
    }

    function refreshImportPreflight() {
        if (pendingImportRootPath.length === 0) {
            catalogAssets.cancelImportPreflight()
            return
        }
        catalogAssets.requestImportPreflight(
                    pendingImportRootPath,
                    pendingImportPaths,
                    importMode,
                    duplicatePolicy,
                    organizationTemplate,
                    rawJpegPolicy)
    }

    function confirmSelectedImport() {
        if (!canConfirmImport)
            return
        importProjectChooserOpen = false
        catalogAssets.importSelectionWithOptions(
                    pendingImportRootPath,
                    pendingImportPaths,
                    projectTarget,
                    importMode,
                    duplicatePolicy,
                    organizationTemplate,
                    rawJpegPolicy)
    }

    function saveCurrentImportPreset(name) {
        const normalizedName = name.trim()
        if (normalizedName.length === 0)
            return false
        const saved = appShell.saveImportPreset(normalizedName, {
            "importMode": importMode,
            "duplicatePolicy": duplicatePolicy,
            "organizationTemplate": organizationTemplate,
            "rawJpegPolicy": rawJpegPolicy,
            "projectTarget": projectTarget
        })
        importPresetStatus = saved ? "Saved “" + normalizedName + "”." : "Could not save this preset."
        return saved
    }

    function loadImportPreset(name) {
        const preset = appShell.importPreset(name)
        if (!preset || !preset.importMode)
            return
        workflowProfile = "studio"
        importMode = preset.importMode || "managed"
        duplicatePolicy = preset.duplicatePolicy || "skip"
        organizationTemplate = preset.organizationTemplate || "source"
        rawJpegPolicy = preset.rawJpegPolicy || "both"
        const savedProjectTarget = preset.projectTarget || ""
        projectTarget = savedProjectTarget
        syncProjectTarget()
        importPresetStatus = projectTarget.length === 0 && savedProjectTarget.length > 0
                ? "Loaded “" + name + "”; its saved project is unavailable, so Photo Library is selected."
                : "Loaded “" + name + "”."
        refreshImportPreflight()
    }

    function importCurrentFolder() {
        if (currentFolderPath.length === 0)
            return
        pendingImportKind = "folder"
        reviewPathsExpanded = false
        copiedReviewPath = ""
        pendingImportRootPath = currentFolderPath
        pendingImportPaths = []
        importProjectChooserOpen = true
        refreshImportPreflight()
    }

    function importActionMenuItems() {
        return [
            {
                key: "import-folder",
                label: "Review Current Folder",
                icon: "folder",
                enabled: root.canImportFolder,
                disabledReason: root.sourceRootIsFileSelection
                                ? "Choose a source folder to import its contents; this picker contains only the selected files."
                                : (root.currentFolderPath.length > 0 ? "No importable media found in this folder." : "Choose a folder first."),
                tooltip: "Review the current folder and its subfolders before confirming import."
            },
            {
                key: "import-with-preset",
                label: "Review With Preset",
                icon: "swatch",
                enabled: root.canImportSelection || root.canImportFolder,
                disabledReason: "Select media or choose a folder first.",
                tooltip: "Review files with a saved import preset."
            },
            {
                key: "select-visible",
                label: "Select Visible Media",
                icon: "check",
                enabled: root.filteredFileCount > 0,
                disabledReason: "No visible media matches the current filters.",
                tooltip: "Select all currently visible media."
            },
            {
                key: "clear-selection",
                label: "Clear Selection",
                icon: "delete",
                enabled: root.selectedFileCount > 0,
                disabledReason: "No files are selected.",
                tooltip: "Clear the staged import selection."
            },
            {
                key: "refresh-sources",
                label: "Refresh Sources",
                icon: "source",
                enabled: !catalogAssets.busy,
                disabledReason: "Source refresh is already running.",
                tooltip: "Rescan mounted storage and import sources."
            }
        ]
    }

    function handleImportMenuAction(key) {
        if (key === "import-folder") {
            importCurrentFolder()
        } else if (key === "import-with-preset") {
            workflowProfile = "studio"
            advancedImportOptionsOpen = true
            if (canImportSelection)
                importSelected()
            else if (canImportFolder)
                importCurrentFolder()
        } else if (key === "select-visible") {
            selectAllVisible()
        } else if (key === "clear-selection") {
            clearSelection()
        } else if (key === "refresh-sources") {
            catalogAssets.refreshImportSources(true)
        }
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

    function importActivityTitle() {
        if (!hasActiveImportJobs())
            return ""
        const reviewed = activeImportJob.metadata && activeImportJob.metadata.importPreflight
                         ? Number(activeImportJob.metadata.importPreflight.importableCount || 0) : 0
        if (reviewed > 0)
            return "Importing " + reviewed + (reviewed === 1 ? " file" : " files")
        return activeImportJob.displayLabel && activeImportJob.displayLabel.length > 0
                ? "Importing " + activeImportJob.displayLabel
                : "Importing media"
    }

    function importActivityProgress() {
        if (!activeImportJob.jobId)
            return -1
        const status = activeImportJob.status ? String(activeImportJob.status) : ""
        const raw = activeImportJob.progress !== undefined ? Number(activeImportJob.progress) : 0
        if (status === "Queued")
            return -1
        if (status === "Paused")
            return Math.max(0, Math.min(1, raw / 100.0))
        if (raw > 0)
            return Math.max(0, Math.min(1, raw / 100.0))
        return -1
    }

    function importSummaryMetadata() {
        if (!activeImportJob.metadata)
            return ({})
        if (activeImportJob.metadata.importResult)
            return activeImportJob.metadata.importResult
        if (activeImportJob.metadata.importPreflight)
            return activeImportJob.metadata.importPreflight
        return ({})
    }

    function importCompletedCount(summary) {
        return Number(summary.importedCount !== undefined
                      ? summary.importedCount : (summary.importableCount || 0))
    }

    function importBackupReceiptText() {
        const summary = importSummaryMetadata()
        if (!summary.backupEnabled)
            return "Originals backup was not enabled for this import."

        const copied = Number(summary.backedUpCount || 0)
        const reused = Number(summary.reusedBackupCount || 0)
        const verifiedCount = copied + reused
        const method = String(summary.verificationAlgorithm || "SHA-256").toUpperCase()
        const destination = summary.backupDestination || summary.backupDestinationPath || "the configured backup"
        return "Verified backup · " + verifiedCount + " copies · " + method + " · " + destination
    }

    function importBackupReceiptSummaryText() {
        const summary = importSummaryMetadata()
        if (!summary.backupEnabled)
            return "Originals backup · Off"
        const verifiedCount = Number(summary.backedUpCount || 0) + Number(summary.reusedBackupCount || 0)
        const destination = String(summary.backupDestination || summary.backupDestinationPath || "")
        return "Verified backup · " + verifiedCount + (verifiedCount === 1 ? " copy" : " copies") +
               " · " + String(summary.verificationAlgorithm || "SHA-256").toUpperCase() +
               " · " + (root.fileNameForPath(destination) || "Configured")
    }

    function importCompletionSummaryText() {
        const summary = importSummaryMetadata()
        const status = activeImportJob.status ? String(activeImportJob.status) : ""
        if (status === "Completed") {
            return "Imported " + importCompletedCount(summary) +
                    " · resumed " + (summary.resumedCount || 0) +
                    " · duplicates " + (summary.duplicateCount || 0) +
                    " · unsupported " + (summary.unsupportedCount || summary.skippedUnsupported || 0) +
                    " · unreadable/failures " + (summary.failureCount || summary.unreadableCount || 0)
        }
        if (status === "Failed")
            return activeImportJob.errorMessage || "Import failed. Retry keeps the same reviewed options."
        if (status === "Canceled")
            return "Canceled safely. Unfinished partial copies were removed."
        if (status === "Paused")
            return "Paused safely. Resume to replay the same reviewed files and options."
        if (status === "Pausing")
            return "Pausing at a safe file boundary…"
        if (status === "Canceling")
            return "Stopping after the current safe file boundary…"
        if (status === "Running") {
            const percent = Number(activeImportJob.progress || 0)
            return "Import in progress" + (percent > 0 ? " · " + percent + "%" : "")
        }
        return root.importJobStatus()
    }

    function importWorkflowPolishSignature() {
        const hasJob = root.hasActiveImportJobs()
        return "import-workflow-polish:v3" +
               "|source:" + (root.hasSource ? "selected" : "none") +
               "|folder:" + (root.currentFolderPath.length > 0 ? "selected" : "none") +
               "|visible:" + String(root.filteredFileCount) +
               "|selected:" + String(root.selectedFileCount) +
               "|selected-photos:" + String(root.selectedPhotoCount) +
               "|selected-videos:" + String(root.selectedVideoCount) +
               "|photos:" + String(root.visiblePhotoCount) +
               "|videos:" + String(root.visibleVideoCount) +
               "|source-confirmation:" + root.sourceConfirmationText() +
               "|filter:" + root.mediaQuickFilter +
               "|destination:" + (root.importProjectChooserOpen ? "open" : "closed") +
               "|can-import-selection:" + String(root.canImportSelection) +
               "|persistent-primary:" + String(root.canActivatePersistentImport) +
               "|primary-disabled-reason:" + root.persistentImportDisabledReason() +
               "|can-import-folder:" + String(root.canImportFolder) +
               "|progress:" + (hasJob ? "active" : "idle") +
               "|activity-title:" + (hasJob ? root.importActivityTitle() : "none") +
               "|preserve:choose-source-select-visible-clear-selection-import-selected-import-folder-refresh-project-target"
    }

    Component.onCompleted: {
        catalogAssets.refreshImportSources()
        syncProjectTarget()
        ensureDefaultSource()
    }

    onVisibleChanged: {
        if (!visible) {
            catalogAssets.cancelImportPreflight()
            return
        }
        catalogAssets.refreshImportSources()
        ensureDefaultSource()
        if (importProjectChooserOpen)
            refreshImportPreflight()
    }

    Component.onDestruction: catalogAssets.cancelImportPreflight()

    onImportProjectChooserOpenChanged: {
        if (!importProjectChooserOpen)
            catalogAssets.cancelImportPreflight()
    }

    onSelectedFileCountChanged: {
        if (selectedFileCount === 0 && pendingImportKind === "selection")
            importProjectChooserOpen = false
    }

    onSelectionRevisionChanged: {
        if (!importProjectChooserOpen || pendingImportKind !== "selection")
            return
        pendingImportPaths = selectedFilePaths
        refreshImportPreflight()
    }

    onImportModeChanged: {
        if (importMode === "referenced")
            organizationTemplate = "source"
        if (importProjectChooserOpen)
            refreshImportPreflight()
    }

    onFilenameQueryChanged: selectionAnchorPath = ""
    onMediaQuickFilterChanged: selectionAnchorPath = ""

    onWorkflowProfileChanged: {
        if (workflowProfile === "personal") {
            importMode = "managed"
            duplicatePolicy = "skip"
            organizationTemplate = "date-camera"
            rawJpegPolicy = "both"
        }
        if (appShell.importWorkflowProfile !== workflowProfile)
            appShell.importWorkflowProfile = workflowProfile
        if (importProjectChooserOpen)
            refreshImportPreflight()
    }

    onDuplicatePolicyChanged: {
        if (importProjectChooserOpen)
            refreshImportPreflight()
    }

    onOrganizationTemplateChanged: {
        if (importProjectChooserOpen)
            refreshImportPreflight()
    }

    onRawJpegPolicyChanged: {
        if (importProjectChooserOpen)
            refreshImportPreflight()
    }

    Connections {
        target: catalogAssets

        function onProjectNamesChanged() {
            root.syncProjectTarget()
        }

        function onImportSourcesChanged() {
            if (root.sourceRootPath.length === 0) {
                root.ensureDefaultSource()
                return
            }

            if (root.sourceRootIsFileSelection && root.stagedPickerFilePaths.length > 0)
                return

            for (let index = 0; index < catalogAssets.importSources.length; ++index) {
                const source = catalogAssets.importSources[index]
                if (source.path === root.sourceRootPath)
                    return
            }

            if (catalogAssets.inspectImportDrop([root.sourceRootUrl]).folderPath)
                return

            root.sourceRootPath = ""
            root.sourceRootUrl = ""
            root.sourceLabel = ""
            root.sourceRootIsFileSelection = false
            root.stagedPickerFilePaths = []
            root.currentFolderPath = ""
            root.currentFolderUrl = ""
            root.pendingFolderPath = ""
            root.pendingFolderUrl = ""
            root.ensureDefaultSource()
        }

        function onImportingChanged() {
            if (!catalogAssets.importing && catalogAssets.errorMessage.length === 0)
                root.clearSelection()
        }
    }

    Connections {
        target: appShell
        function onStorageLocationsChanged() {
            if (root.importProjectChooserOpen) {
                catalogAssets.cancelImportPreflight()
                root.refreshImportPreflight()
            }
        }
    }

    Timer {
        id: deferredFolderLoadTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (root.pendingFolderPath.length === 0 || root.pendingFolderUrl.length === 0)
                return
            root.currentFolderPath = root.pendingFolderPath
            root.currentFolderUrl = root.pendingFolderUrl
            root.pendingFolderPath = ""
            root.pendingFolderUrl = ""
        }
    }

    FolderListModel {
        id: directoryModel
        folder: root.currentFolderUrl.length > 0 ? root.currentFolderUrl : "file:///__looksy_no_import_source__"
        showDirs: true
        showFiles: false
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        sortCaseSensitive: false
    }

    FolderListModel {
        id: fileModel
        folder: root.currentFolderUrl.length > 0 ? root.currentFolderUrl : "file:///__looksy_no_import_source__"
        showDirs: false
        showFiles: true
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        sortCaseSensitive: false
        nameFilters: catalogAssets.importFileNameFilters
    }

    AppDialog {
        id: mediaPreviewDialog
        objectName: "importMediaPreviewDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(popupAvailableWidth, theme.scaled(1120))
        height: Math.min(popupAvailableHeight, theme.scaled(840))
        title: root.fileNameForPath(root.previewCurrentPath)
        onClosed: {
            root.previewIndex = -1
            root.previewPaths = []
            if (!root.importProjectChooserOpen) {
                const card = mediaGrid.itemAtIndex(mediaGrid.currentIndex)
                if (card) card.forceActiveFocus()
            }
        }
        contentItem: ColumnLayout {
            spacing: theme.space12
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8
                ImportDialogButton {
                    objectName: "importPreviewFitButton"
                    text: "Fit"
                    compact: true
                    embedded: true
                    active: root.previewZoom === 0
                    onClicked: root.setPreviewZoom(0)
                }
                ImportDialogButton {
                    objectName: "importPreviewActualSizeButton"
                    text: "100%"
                    compact: true
                    embedded: true
                    active: root.previewZoom === 1
                    onClicked: root.setPreviewZoom(1)
                }
                ImportDialogButton {
                    text: "−"
                    accessibleName: "Zoom out"
                    compact: true
                    embedded: true
                    onClicked: root.setPreviewZoom((root.previewZoom || previewFlick.fitScale) / 1.5)
                }
                ImportDialogButton {
                    text: "+"
                    accessibleName: "Zoom in"
                    compact: true
                    embedded: true
                    onClicked: root.setPreviewZoom((root.previewZoom || previewFlick.fitScale) * 1.5)
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.previewZoom === 0 ? "Fit to window" : Math.round(root.previewZoom * 100) + "% · Drag to pan"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                }
            }
            Flickable {
                id: previewFlick
                objectName: "importPreviewPanSurface"
                Layout.fillWidth: true
                Layout.fillHeight: true
                readonly property real nativeWidth: Math.max(1, largePreviewImage.implicitWidth)
                readonly property real nativeHeight: Math.max(1, largePreviewImage.implicitHeight)
                readonly property real fitScale: Math.min(width * Screen.devicePixelRatio / nativeWidth, height * Screen.devicePixelRatio / nativeHeight)
                contentWidth: Math.max(width, largePreviewImage.width)
                contentHeight: Math.max(height, largePreviewImage.height)
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentWidth > width + 1 || contentHeight > height + 1
                clip: true
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                Rectangle { width: previewFlick.contentWidth; height: previewFlick.contentHeight; color: theme.mediaCanvas }
                Image {
                    id: largePreviewImage
                    objectName: "importLargePreviewImage"
                    x: Math.max(0, (previewFlick.width - width) / 2)
                    y: Math.max(0, (previewFlick.height - height) / 2)
                    width: previewFlick.nativeWidth / Screen.devicePixelRatio * (root.previewZoom || previewFlick.fitScale)
                    height: previewFlick.nativeHeight / Screen.devicePixelRatio * (root.previewZoom || previewFlick.fitScale)
                    source: mediaPreviewDialog.opened && !root.isVideoFile(root.previewCurrentPath)
                            ? catalogAssets.originalPreviewSource(root.previewCurrentPath) : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    visible: status === Image.Ready
                }
                OpticalActivityIndicator {
                    x: Math.max(0, previewFlick.width - width - theme.space14)
                    y: Math.max(0, previewFlick.height - height - theme.space14)
                    width: theme.scaled(18)
                    height: width
                    active: largePreviewImage.status === Image.Loading
                    visible: active
                }
                Label {
                    x: 0; y: Math.max(0, (previewFlick.height - height) / 2)
                    width: previewFlick.width
                    horizontalAlignment: Text.AlignHCenter
                    text: root.isVideoFile(root.previewCurrentPath) ? "Video file"
                          : largePreviewImage.status === Image.Loading ? "Loading preview…" : "Preview unavailable"
                    visible: largePreviewImage.status !== Image.Ready && largePreviewImage.status !== Image.Loading
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WordWrap
                }
            }
            Label {
                objectName: "importPreviewMetadata"
                Layout.fillWidth: true
                text: [root.previewFileDetails.format || "", root.previewFileDetails.width > 0 ? root.previewFileDetails.width + " × " + root.previewFileDetails.height : "", root.previewFileDetails.sizeText || ""].filter(function(value) { return value.length > 0 }).join(" · ")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: (root.previewIndex + 1) + " / " + root.previewPaths.length
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                ImportDialogButton {
                    visible: root.isVideoFile(root.previewCurrentPath)
                    text: "Open video"
                    compact: true
                    onClicked: Qt.openUrlExternally(root.fileUrlForPath(root.previewCurrentPath))
                }
                ImportDialogButton {
                    objectName: "importPreviewSelectionButton"
                    text: root.previewCurrentSelected ? "Deselect" : "Select"
                    active: root.previewCurrentSelected
                    compact: true
                    onClicked: root.toggleSelection(root.previewCurrentPath)
                }
            }
        }
        footer: Item {
            implicitHeight: previewFooter.implicitHeight + theme.popupPadding * 2
            RowLayout {
                id: previewFooter
                anchors.fill: parent
                anchors.margins: theme.popupPadding
                spacing: theme.space8
                ImportDialogButton {
                    objectName: "importPreviewPreviousButton"
                    text: "Previous"
                    iconName: "chevron-left"
                    compact: true
                    enabled: root.previewIndex > 0
                    onClicked: root.previewIndex -= 1
                }
                ImportDialogButton {
                    objectName: "importPreviewNextButton"
                    text: "Next"
                    compact: true
                    enabled: root.previewIndex + 1 < root.previewPaths.length
                    onClicked: root.previewIndex += 1
                }
                Item { Layout.fillWidth: true }
                ImportDialogButton {
                    objectName: "importPreviewCloseButton"
                    text: "Done"
                    tone: "primary"
                    compact: true
                    onClicked: mediaPreviewDialog.close()
                }
            }
        }
        Shortcut {
            sequences: ["Left"]
            enabled: mediaPreviewDialog.opened && root.previewIndex > 0
            onActivated: root.previewIndex -= 1
        }
        Shortcut {
            sequences: ["Right"]
            enabled: mediaPreviewDialog.opened && root.previewIndex + 1 < root.previewPaths.length
            onActivated: root.previewIndex += 1
        }
    }

    AppDialog {
        id: sourceDetailsDialog
        objectName: "importSourceDetailsDialog"
        property Item opener: null
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(popupAvailableWidth, theme.scaled(680))
        title: "Folder details"
        onClosed: if (opener) opener.forceActiveFocus(Qt.OtherFocusReason)
        contentItem: ColumnLayout {
            spacing: theme.space12
            BodyLabel {
                objectName: "importSourceFullPath"
                Layout.fillWidth: true
                text: root.currentFolderPath
                font.family: theme.fontNumeric
                font.features: theme.numericFontFeatures
                font.pixelSize: theme.typeCaption
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    objectName: "importCopyFolderDetailsPathButton"
                    text: "Copy path"
                    iconName: "copy"
                    onClicked: appShell.copyTextToClipboard(root.currentFolderPath)
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "importCloseSourceDetailsButton"
                    text: "Close"
                    onClicked: sourceDetailsDialog.close()
                }
            }
        }
    }

    AppDialog {
        id: importStorageDialog
        objectName: "importStorageDialog"
        property bool showLocations: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(popupAvailableWidth, theme.scaled(480))
        title: "Import storage"
        onOpened: showLocations = false
        contentItem: ColumnLayout {
            spacing: theme.space16
            RowLayout {
                Layout.fillWidth: true
                ReviewLocation {
                    caption: "Photo Library"
                    showPath: false
                    path: appShell.photoLibraryLocation
                    summary: root.fileNameForPath(path) === "managed" ? "Looksy Library" : root.fileNameForPath(path)
                }
                ImportDialogButton {
                    text: "Change…"
                    compact: true
                    enabled: !appShell.photoLibraryMigrationRunning && !root.hasActiveImportJobs()
                    tooltipText: "Move the library with verification. The previous copy is retained."
                    onClicked: root.chooseImportStorage(false)
                }
            }
            BodyLabel {
                Layout.fillWidth: true
                visible: importStorageDialog.showLocations
                text: appShell.photoLibraryLocation
                font.pixelSize: theme.typeCaption
            }
            RowLayout {
                Layout.fillWidth: true
                ReviewLocation {
                    caption: "Originals backup"
                    showPath: false
                    path: appShell.originalsBackupLocation
                    summary: appShell.originalsBackupEnabled ? root.fileNameForPath(path) : "Off"
                }
                ImportDialogButton {
                    text: appShell.originalsBackupEnabled ? "Change…" : "Set up…"
                    compact: true
                    enabled: !appShell.photoLibraryMigrationRunning && !root.hasActiveImportJobs()
                    onClicked: root.chooseImportStorage(true)
                }
                ImportDialogButton {
                    text: "Turn off"
                    compact: true
                    visible: appShell.originalsBackupEnabled
                    enabled: !root.hasActiveImportJobs()
                    onClicked: { appShell.disableOriginalsBackup(); root.storageFeedback = appShell.storageConfigurationStatus }
                }
            }
            BodyLabel {
                Layout.fillWidth: true
                visible: importStorageDialog.showLocations && appShell.originalsBackupEnabled
                text: appShell.originalsBackupLocation
                font.pixelSize: theme.typeCaption
            }
            BodyLabel {
                Layout.fillWidth: true
                text: appShell.photoLibraryMigrationRunning ? appShell.storageConfigurationStatus : root.storageFeedback
                visible: text.length > 0
                font.pixelSize: theme.typeCaption
            }
            RowLayout {
                Layout.fillWidth: true
                ImportDialogButton {
                    text: importStorageDialog.showLocations ? "Hide locations" : "Show locations"
                    compact: true
                    onClicked: importStorageDialog.showLocations = !importStorageDialog.showLocations
                }
                Item { Layout.fillWidth: true }
                ImportDialogButton {
                    objectName: "importStorageDoneButton"
                    text: "Done"
                    tone: "primary"
                    onClicked: importStorageDialog.close()
                }
            }
        }
    }

    AppDialog {
        id: duplicateReviewDialog
        objectName: "importDuplicateReviewDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(popupAvailableWidth, theme.scaled(1020))
        height: Math.min(popupAvailableHeight, theme.scaled(680))
        title: "Existing file matches"
        contentItem: ColumnLayout {
            spacing: theme.space12
            Label {
                Layout.fillWidth: true
                text: "Matching filenames. Compare before replacing; file contents may differ."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: theme.space12
                Repeater {
                    model: ["Incoming", "Existing"]
                    delegate: ColumnLayout {
                        id: comparison
                        required property string modelData
                        readonly property string filePath: modelData === "Incoming" ? root.duplicateReviewMatch.sourcePath || "" : root.duplicateReviewMatch.existingPath || ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Label { text: comparison.modelData; font.family: theme.fontUi; font.pixelSize: theme.typeCaption; color: theme.textStrong }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: theme.mediaCanvas
                            Image {
                                objectName: comparison.modelData === "Incoming" ? "importDuplicateIncomingImage" : "importDuplicateExistingImage"
                                id: duplicateImage
                                anchors.fill: parent
                                source: duplicateReviewDialog.opened && !root.duplicateReviewMatch.isVideo ? catalogAssets.originalPreviewSource(comparison.filePath) : ""
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: !root.duplicateReviewMatch.isVideo && duplicateImage.status === Image.Error
                                text: duplicateImage.status === Image.Loading ? "Loading preview…" : "Preview unavailable"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                            }
                            OpticalActivityIndicator {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: theme.space14
                                width: theme.scaled(18)
                                height: width
                                active: duplicateImage.status === Image.Loading
                                visible: active
                            }
                            ImportDialogButton {
                                anchors.centerIn: parent
                                visible: !!root.duplicateReviewMatch.isVideo
                                text: "Open video"
                                compact: true
                                onClicked: Qt.openUrlExternally(root.fileUrlForPath(comparison.filePath))
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            id: comparisonFilename
                            text: root.fileNameForPath(comparison.filePath)
                            Accessible.description: comparison.filePath
                            HoverHandler { id: comparisonNameHover }
                            AppToolTip { targetItem: comparisonFilename; visible: comparisonNameHover.hovered; text: comparison.filePath }
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            color: theme.textMuted
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ImportDialogButton { text: "Previous"; compact: true; enabled: root.duplicateReviewIndex > 0; onClicked: root.duplicateReviewIndex -= 1 }
                ImportDialogButton { text: "Next"; compact: true; enabled: root.duplicateReviewIndex + 1 < root.duplicateReviewMatches.length; onClicked: root.duplicateReviewIndex += 1 }
                Label {
                    Layout.fillWidth: true
                    text: (root.duplicateReviewIndex + 1) + " of " + root.duplicateReviewMatches.length + (root.importPreflight.duplicateCount > root.duplicateReviewMatches.length ? " shown" : "")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                }
                ImportDialogButton { objectName: "importDuplicateKeepButton"; text: "Skip matches"; active: root.duplicatePolicy === "skip"; compact: true; onClicked: { root.duplicatePolicy = "skip"; duplicateReviewDialog.close() } }
                ImportDialogButton { objectName: "importDuplicateReplaceButtonInDialog"; text: "Replace matches"; active: root.duplicatePolicy === "replace"; compact: true; onClicked: { root.workflowProfile = "studio"; root.duplicatePolicy = "replace"; duplicateReviewDialog.close() } }
                ImportDialogButton { objectName: "importDuplicateDoneButton"; text: "Done"; compact: true; onClicked: duplicateReviewDialog.close() }
            }
        }
    }

    DropArea {
        id: importDropArea
        objectName: "importDropArea"
        anchors.fill: parent
        enabled: !catalogAssets.busy && !root.importProjectChooserOpen && !mediaPreviewDialog.opened
        onEntered: function(drag) { drag.accepted = drag.hasUrls }
        onDropped: function(drop) { drop.accepted = root.stageDroppedUrls(drop.urls) }
    }

    Rectangle {
        anchors.fill: parent
        z: 10
        visible: importDropArea.containsDrag
        color: theme.alpha(theme.ink950, 0.94)
        border.width: theme.focusBorderWidth
        border.color: theme.focusRing
        radius: theme.radius4
        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - theme.space24, theme.scaled(420))
            spacing: theme.space12
            Label {
                width: parent.width
                text: "Drop media to review"
                horizontalAlignment: Text.AlignHCenter
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: theme.typePageTitle
            }
            Label {
                width: parent.width
                text: "Drop individual files or one folder. Import starts only after you review and confirm."
                horizontalAlignment: Text.AlignHCenter
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.compactImportDensity ? theme.space6 : theme.space12

        RowLayout {
            id: importWorkspacePanels
            objectName: "importWorkspacePanels"
            Layout.fillWidth: true
            // Keep the panels inside the space left above the persistent footer.
            // A fill-height preference can otherwise let tall source content
            // compete with the footer when text is enlarged.
            Layout.fillHeight: false
            Layout.preferredHeight: Math.max(0, root.height - importPersistentActionBar.implicitHeight -
                (root.compactImportDensity ? theme.space6 : theme.space12))
            Layout.maximumHeight: Math.max(0, root.height - importPersistentActionBar.implicitHeight -
                (root.compactImportDensity ? theme.space6 : theme.space12))
            Layout.minimumHeight: 0
            spacing: theme.space12

            BrandPanel {
                objectName: "importSourcePanel"
                readonly property string visibilityRole: "always-visible|source-list-folder-nav-folder-list-refresh"
                visible: !root.importProjectChooserOpen
                Layout.preferredWidth: theme.scaled(208)
                Layout.minimumWidth: theme.scaled(176)
                Layout.minimumHeight: 0
                Layout.fillHeight: true
                clip: true
                title: "Sources"
                subtitle: ""
                accent: theme.accentCool400
                headerVisible: false
                bodyMargin: theme.panelPadding
                bodySpacing: theme.space8

                Flickable {
                    id: importSourceScroll
                    objectName: "importSourceScrollArea"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    clip: true
                    contentWidth: width
                    contentHeight: importSourceContents.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: importSourceContents
                        width: importSourceScroll.width
                        spacing: root.compactImportDensity ? theme.space4 : theme.space8

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                Label {
                    Layout.fillWidth: true
                    text: "Sources"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: Font.DemiBold
                }

                ActionButton {
                    showText: false
                    iconName: "refresh"
                    compact: true
                    embedded: true
                    tooltipText: "Refresh sources"
                    enabled: !catalogAssets.busy
                    onClicked: catalogAssets.refreshImportSources(true)
                }
            }

            ActionButton {
                Layout.fillWidth: true
                visible: false
                text: "Choose folder…"
                iconName: "folder"
                compact: true
                tooltipText: "Browse a local folder or connected volume"
                onClicked: root.browseForSource()
            }

            ActionButton {
                Layout.fillWidth: true
                visible: false
                text: "Choose files…"
                iconName: "media"
                compact: true
                embedded: true
                tooltipText: root.supportedImportTypesText()
                onClicked: root.browseForFiles()
            }

            SplitButton {
                Layout.fillWidth: true
                // The central no-source state owns first-run folder/file
                // choices. Once a source is open, keep replacement choices
                // here and let the empty state offer files only.
                visible: root.hasSource
                text: "Choose folder…"
                iconName: "folder"
                menuTooltipText: "Choose a source folder or individual files"
                menuItems: [{key: "files", label: "Choose files…", icon: "media"}]
                onClicked: root.browseForSource()
                onMenuItemTriggered: function(key) { if (key === "files") root.browseForFiles() }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: theme.hairlineWidth
                color: theme.alpha(theme.lineSoft, 0.3)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6
            ToneComboBox {
                objectName: "importSourceBrowserMode"
                Layout.fillWidth: true
                accessibleLabel: "Import source list"
                model: ["Locations", "Recent", "Favorites"]
                currentIndex: root.sourceBrowserMode === "recent" ? 1 : root.sourceBrowserMode === "favorites" ? 2 : 0
                onActivated: function(index) { root.sourceBrowserMode = ["locations", "recent", "favorites"][index] }
            }
            ActionButton {
                objectName: "importFavoriteSourceButton"
                text: root.savedFavoriteSources.indexOf(root.sourceRootPath) >= 0 ? "Remove favorite" : "Favorite this source"
                showText: false
                iconName: "star"
                active: root.savedFavoriteSources.indexOf(root.sourceRootPath) >= 0
                tooltipText: text
                embedded: true
                compact: true
                enabled: root.hasSource
                onClicked: root.toggleSourceFavorite()
            }
            }
            Label {
                Layout.fillWidth: true
                visible: root.sourceBrowserMode !== "locations" && root.sourceBrowserEntries.length === 0
                text: root.sourceBrowserMode === "favorites" ? "Favorite a source to keep it here." : "Sources you open will appear here."
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                color: theme.textMuted
                wrapMode: Text.WordWrap
            }
            ListView {
                id: sourceListView
                objectName: "importSourceList"
                readonly property string visibilityRole: "always-visible|mounted-sources-select"
                Layout.fillWidth: true
                Layout.minimumHeight: 0
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: Math.min(contentHeight, root.compactSourceControls ? Math.max(theme.controlHeight, root.height * (root.compactImportDensity ? 0.1 : 0.16)) : Math.max(theme.scaled(184), root.height * 0.4))
                Layout.fillHeight: false
                clip: true
                spacing: theme.space4
                model: root.sourceBrowserEntries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: sourceDelegate
                    required property var modelData

                    width: ListView.view ? ListView.view.width : 0
                    height: sourceRow.implicitHeight

                    ImportSourceRow {
                        id: sourceRow
                        anchors.fill: parent
                        label: String(sourceDelegate.modelData.label || "Location")
                        subtitle: String(sourceDelegate.modelData.subtitle || "Local storage")
                        path: String(sourceDelegate.modelData.path || "")
                        sourceUrl: String(sourceDelegate.modelData.url || "")
                        selected: root.sourceRootPath === path
                        onActivated: function(path, sourceUrl, label) {
                            root.openSavedSource(path)
                        }
                    }
                }
            }

            Label {
                visible: catalogAssets.importSources.length === 0
                Layout.fillWidth: true
                text: "No import sources are available yet. Refresh sources to rescan mounted storage."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
            }

            Label {
                visible: root.hasSource && !root.sourceRootIsFileSelection
                Layout.fillWidth: true
                text: "Folders" + (directoryModel.count > 0 ? "  ·  " + directoryModel.count : "")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: Font.DemiBold
            }

            Rectangle {
                objectName: "importFolderNavigation"
                readonly property string visibilityRole: "contextual|up-current-folder-source-folder-count"
                visible: root.hasSource && !root.sourceRootIsFileSelection
                Layout.fillWidth: true
                implicitHeight: folderNavRow.implicitHeight
                color: "transparent"
                border.width: 0

                RowLayout {
                    id: folderNavRow
                    anchors.fill: parent
                    anchors.margins: 0
                    spacing: theme.space6

                    ActionButton {
                        iconName: "chevron-left"
                        showText: false
                        tooltipText: "Up one folder"
                        compact: true
                        embedded: true
                        enabled: root.canGoUp
                        onClicked: root.goUp()
                    }

                    BodyLabel {
                        objectName: "importCurrentFolderLabel"
                        Layout.fillWidth: true
                        text: root.currentFolderName
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: Font.Medium
                        fontSizeMode: Text.FixedSize
                        elide: Text.ElideNone
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                }
            }

            ListView {
                id: folderListView
                objectName: "importFolderList"
                readonly property string visibilityRole: "contextual|folder-browse-keyboard"
                visible: root.hasSource && !root.sourceRootIsFileSelection
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.minimumHeight: 0
                Layout.preferredHeight: Math.min(contentHeight, root.compactSourceControls
                    ? Math.max(theme.controlHeight, root.height * 0.12)
                    : Math.max(theme.scaled(120), root.height * 0.2))
                clip: true
                spacing: theme.space4
                model: directoryModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: folderDelegate
                    required property string fileName
                    required property string fileUrl

                    width: ListView.view ? ListView.view.width : 0
                    height: folderRow.implicitHeight

                    ImportFolderRow {
                        id: folderRow
                        anchors.fill: parent
                        fileName: folderDelegate.fileName
                        fileUrl: folderDelegate.fileUrl
                        selected: root.currentFolderUrl === fileUrl
                        onActivated: function(fileUrl) { root.openFolder(fileUrl) }
                    }
                }
            }

            Item {
                Layout.fillHeight: false
                visible: !root.hasSource || root.sourceRootIsFileSelection
            }

                    }
                }

        }

            Rectangle {
                objectName: "importMainPanel"
                readonly property string surfaceContract: "import-main-panel:v1|header:always|media-grid:always|filter:always|selection-actions:always|empty-states:contextual|primary-import:always|no-elide:true|no-overlap:true"
                visible: !root.importProjectChooserOpen
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                radius: 0
                color: "transparent"
                border.width: 0
                clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 0
                spacing: root.compactImportDensity ? theme.space4 : theme.space10

                Rectangle {
                    objectName: "importHeaderBar"
                    readonly property string visibilityRole: "always-visible|title-selected-count-select-visible-clear-import"
                    Layout.fillWidth: true
                    Layout.preferredHeight: importHeaderContent.implicitHeight + (root.compactImportDensity ? theme.space8 : theme.space16)
                    radius: theme.radius4
                    color: "transparent"
                    border.width: 0

                    GridLayout {
                        id: importHeaderContent
                        anchors.fill: parent
                        anchors.margins: theme.space8
                        columns: width < theme.scaled(280) ? 1 : 2
                        columnSpacing: theme.space12
                        rowSpacing: root.compactImportDensity ? theme.space4 : theme.space8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space4

                            Label {
                                Layout.fillWidth: true
                                text: root.hasSource ? "Select media" : "Import"
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: theme.typeTitle
                                font.weight: Font.DemiBold
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: 1
                                elide: Text.ElideNone
                            }

                            RowLayout {
                                Layout.fillWidth: false
                                spacing: theme.space10

                                ActionButton {
                                    id: sourceDetailsButton
                                    objectName: "importSourceDetailsButton"
                                    visible: root.currentFolderPath.length > 0
                                    text: "Source details"
                                    showText: false
                                    iconName: "folder"
                                    embedded: true
                                    compact: true
                                    tooltipText: root.currentFolderPath
                                    onClicked: { sourceDetailsDialog.opener = sourceDetailsButton; sourceDetailsDialog.open() }
                                }
                                Label {
                                    objectName: "importSourceConfirmationChip"
                                    visible: false
                                    text: root.pendingFolderPath.length > 0 ? "Opening…" : root.sourceRootIsFileSelection ? "File selection" : root.hasSource ? "Folder source" : "Local import"
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeMicro
                                    font.weight: Font.Medium
                                }

                                Rectangle {
                                    Layout.preferredWidth: theme.scaled(3)
                                    Layout.preferredHeight: theme.scaled(3)
                                    radius: 2
                                    color: theme.textFaint
                                    visible: false
                                }

                                Label {
                                    objectName: "importSelectionMixChip"
                                    visible: false
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    text: root.selectionMixText() + " selected"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeMicro
                                    font.weight: root.selectedFileCount > 0 ? Font.DemiBold : Font.Normal
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignRight
                            columns: 3
                            columnSpacing: theme.space8
                            rowSpacing: theme.space8

                            ActionButton {
                                visible: !root.compactSourceControls && root.filteredFileCount > 0
                                text: "Select all"
                                embedded: true
                                compact: true
                                enabled: root.filteredFileCount > 0
                                onClicked: root.selectAllVisible()
                            }

                            ActionButton {
                                visible: !root.compactSourceControls && root.selectedFileCount > 0
                                iconName: "close"
                                text: "Clear"
                                embedded: true
                                tooltipText: "Clear selection"
                                compact: true
                                enabled: root.selectedFileCount > 0
                                onClicked: root.clearSelection()
                            }

                            MenuButton {
                                objectName: "importHeaderMoreMenuButton"
                                Layout.columnSpan: 1
                                Layout.alignment: Qt.AlignRight
                                readonly property string visibilityRole: "always-visible|secondary-import-and-selection-options"
                                text: "More"
                                iconName: "more"
                                primaryOpensMenu: true
                                showMenuIndicator: false
                                secondaryActionMenu: true
                                menuTooltipText: "More import options"
                                menuItems: root.importActionMenuItems()
                                tooltipText: "Review a folder import, use a preset, or change selection."
                                onMenuItemTriggered: function(key) { root.handleImportMenuAction(key) }
                            }
                        }
                    }
                }

                Label {
                    objectName: "importDropFeedback"
                    Layout.fillWidth: true
                    visible: root.dropMessage.length > 0 && (!root.compactSourceControls || root.dropMessageIsError)
                    text: root.dropMessage
                    color: root.dropMessageIsError ? theme.warning : theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    objectName: "importProgressSummaryCard"
                    readonly property string visibilityRole: "contextual|queued-running-canceling-completed-failed-canceled-summary-actions"
                    Layout.fillWidth: true
                    visible: !!root.activeImportJob.jobId && root.activeImportJob.jobId !== root.dismissedImportJobId
                    implicitHeight: importProgressSummaryRow.implicitHeight + (theme.space8 * 2)
                    radius: 0
                    color: "transparent"
                    border.width: 0

                    RowLayout {
                        id: importProgressSummaryRow
                        anchors.fill: parent
                        anchors.margins: theme.space8
                        spacing: theme.space8

                        Item {
                            Layout.preferredWidth: theme.scaled(18)
                            Layout.preferredHeight: theme.scaled(18)
                            OpticalActivityIndicator { anchors.fill: parent; active: visible; visible: root.hasActiveImportJobs() }
                            AppIcon {
                                anchors.fill: parent
                                visible: !root.hasActiveImportJobs()
                                name: root.activeImportJob.status === "Completed" ? "check" : root.activeImportJob.status === "Failed" ? "error" : "media"
                                color: root.activeImportJob.status === "Failed" ? theme.error : theme.textMuted
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: theme.space2

                            Label {
                                Layout.fillWidth: true
                                text: root.hasActiveImportJobs()
                                      ? root.importActivityTitle()
                                      : "Import summary"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                font.weight: Font.DemiBold
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.importCompletionSummaryText()
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Label {
                                id: importBackupReceiptLabel
                                Layout.fillWidth: true
                                visible: root.activeImportJob.status === "Completed"
                                text: root.importBackupReceiptSummaryText()
                                color: root.importSummaryMetadata().backupEnabled ? theme.success : theme.textFaint
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                Accessible.description: root.importBackupReceiptText()
                                HoverHandler { id: importBackupReceiptHover }
                                AppToolTip {
                                    targetItem: importBackupReceiptLabel
                                    visible: importBackupReceiptHover.hovered
                                    text: root.importBackupReceiptText()
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: root.activeImportJob.status === "Failed"
                                text: "Check the source and destination, then retry. The job keeps its reviewed file list and options."
                                color: theme.warning
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            ProgressStrip {
                                objectName: "importLoadingScene"
                                Layout.fillWidth: true
                                Layout.preferredHeight: theme.scaled(4)
                                visible: root.hasActiveImportJobs() || root.activeImportJob.status === "Paused"
                                active: visible && root.activeImportJob.status === "Running"
                                progress: root.importActivityProgress()
                            }
                        }

                        ActionButton {
                            visible: jobQueue && jobQueue.canPauseJob(root.activeImportJob.jobId || "")
                            text: "Pause"
                            iconName: "pause"
                            compact: true
                            onClicked: jobQueue.pauseJob(root.activeImportJob.jobId)
                        }

                        ActionButton {
                            visible: jobQueue && jobQueue.canResumeJob(root.activeImportJob.jobId || "")
                            text: "Resume"
                            iconName: "play"
                            compact: true
                            onClicked: jobQueue.resumeJob(root.activeImportJob.jobId)
                        }

                        ActionButton {
                            visible: jobQueue && jobQueue.canCancelJob(root.activeImportJob.jobId || "")
                            text: "Cancel"
                            iconName: "close"
                            compact: true
                            onClicked: jobQueue.cancelJob(root.activeImportJob.jobId)
                        }

                        ActionButton {
                            visible: root.activeImportJob.status === "Failed" ||
                                     root.activeImportJob.status === "Canceled"
                            enabled: jobQueue && jobQueue.historyMutationsEnabled
                            disabledReason: "Resolve job recovery before changing history."
                            text: "Retry"
                            iconName: "refresh"
                            compact: true
                            onClicked: jobQueue.retryJob(root.activeImportJob.jobId)
                        }

                        ActionButton {
                            visible: root.activeImportJob.status === "Failed"
                            text: "Details"
                            iconName: "info"
                            compact: true
                            tooltipText: "Open the import job details and recovery status"
                            onClicked: root.openJobsWorkspace()
                        }

                        ActionButton {
                            objectName: "importDismissSummaryButton"
                            visible: root.activeImportJob.status === "Completed" || root.activeImportJob.status === "Canceled"
                            text: "Dismiss"
                            compact: true
                            embedded: true
                            onClicked: root.dismissedImportJobId = root.activeImportJob.jobId
                        }

                        ActionButton {
                            objectName: "importReviewImportedButton"
                            visible: root.activeImportJob.status === "Completed" &&
                                     root.importCompletedCount(root.importSummaryMetadata()) > 0
                            text: "Review imported"
                            iconName: "library"
                            compact: true
                            tooltipText: "Open the Photo Library to review the imported media"
                            onClicked: root.reviewImportedRequested()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: root.compactImportDensity ? theme.space4 : theme.space10

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: root.compactImportDensity ? theme.space4 : theme.space8

                            RowLayout {
                                objectName: "importMediaFilterBar"
                                readonly property string visibilityRole: "always-visible|all-photos-videos-selected-filter-summary-reset"
                                Layout.fillWidth: true
                                spacing: theme.space4

                                MediaFilterTab {
                                    text: "All"
                                    selected: root.mediaQuickFilter === "all"
                                    onClicked: root.mediaQuickFilter = "all"
                                }

                                MediaFilterTab {
                                    text: "Photos"
                                    selected: root.mediaQuickFilter === "photos"
                                    onClicked: root.mediaQuickFilter = "photos"
                                }

                                MediaFilterTab {
                                    text: "Videos"
                                    selected: root.mediaQuickFilter === "videos"
                                    onClicked: root.mediaQuickFilter = "videos"
                                }

                                MediaFilterTab {
                                    text: "Selected"
                                    selected: root.mediaQuickFilter === "selected"
                                    onClicked: root.mediaQuickFilter = "selected"
                                }

                                Item { Layout.fillWidth: true }

                                BodyLabel {
                                    objectName: "importVisibleFileCount"
                                    text: root.filteredFileEntries.some(function(entry) { return entry.filePaths && entry.filePaths.length > 1 })
                                          ? root.filteredFileCount + (root.filteredFileCount === 1 ? " capture" : " captures") : root.filteredFileCount + " files"
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeMicro
                                }

                                ActionButton {
                                    iconName: "delete"
                                    showText: false
                                    tooltipText: "Show all media"
                                    compact: true
                                    visible: root.mediaQuickFilter !== "all"
                                    onClicked: root.mediaQuickFilter = "all"
                                }
                            }

                            GridLayout {
                                objectName: "importBrowseTools"
                                Layout.fillWidth: true
                                columns: root.compactSourceControls ? 3 : 5
                                columnSpacing: root.compactImportDensity ? theme.space4 : theme.space8
                                rowSpacing: root.compactImportDensity ? theme.space4 : theme.space6
                                RowLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: root.compactSourceControls ? 3 : 1
                                spacing: theme.space6
                                ToneTextField {
                                    id: filenameSearch
                                    objectName: "importFilenameSearch"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: theme.scaled(120)
                                    implicitHeight: theme.controlHeightDense
                                    placeholderText: "Search filenames"
                                    accessibleLabel: "Search import filenames"
                                    text: root.filenameQuery
                                    onTextEdited: root.filenameQuery = text
                                }
                                ActionButton {
                                    text: "Clear search"
                                    showText: !root.compactSourceControls
                                    iconName: "close"
                                    embedded: true
                                    compact: true
                                    visible: root.filenameQuery.length > 0
                                    onClicked: root.filenameQuery = ""
                                }
                                }
                                ActionButton {
                                    objectName: "importOpenPreviewButton"
                                    text: "Preview"
                                    showText: !root.compactSourceControls
                                    iconName: "image"
                                    tooltipText: "Large preview · Space"
                                    compact: true
                                    embedded: true
                                    enabled: root.filteredFileCount > 0
                                    onClicked: root.openPreview(Math.max(0, mediaGrid.currentIndex))
                                }
                                ActionButton {
                                    objectName: "importGroupPairsButton"
                                    text: "RAW + JPEG"
                                    tooltipText: "Group matching RAW and JPEG files from the same folder"
                                    active: importBrowserSettings.groupRawJpeg
                                    embedded: true
                                    compact: true
                                    onClicked: importBrowserSettings.groupRawJpeg = !importBrowserSettings.groupRawJpeg
                                }
                                Label {
                                    visible: root.width >= theme.scaled(1120)
                                    text: "Size"
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeMicro
                                }
                                ToolValueSlider {
                                    objectName: "importThumbnailSizeSlider"
                                    Layout.preferredWidth: theme.scaled(112)
                                    Layout.minimumWidth: theme.scaled(84)
                                    label: "Thumbnail size"
                                    valueText: Math.round(value) + " pixels"
                                    from: 180
                                    to: 380
                                    stepSize: 20
                                    value: root.thumbnailSize
                                    onMoved: root.thumbnailSize = value
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumHeight: root.compactImportDensity ? theme.scaled(96) : 0
                                GridView {
                                    id: mediaGrid
                                    objectName: "importMediaGrid"
                                    readonly property string visibilityRole: "always-visible|media-preview-selection-keyboard"
                                    anchors.fill: parent
                                    clip: true
                                    property int columns: Math.max(1, Math.floor(width / theme.scaled(root.thumbnailSize)))
                                    property int gutter: theme.space12
                                    readonly property bool compactPreview: root.height < theme.scaled(760)
                                    readonly property real compactPreviewAspectRatio: 0.72
                                    readonly property int compactPreviewMinimumHeight: compactPreview ? 110 : 142
                                    cellWidth: Math.floor(width / columns)
                                    cellHeight: Math.max(compactPreviewMinimumHeight,
                                                         (cellWidth - gutter) * compactPreviewAspectRatio) + 32
                                    boundsBehavior: Flickable.StopAtBounds
                                    model: root.filteredFileEntries
                                    keyNavigationEnabled: true
                                    currentIndex: count > 0 ? 0 : -1

                                    delegate: FocusScope {
                                        id: importMediaCard
                                        required property int index
                                        required property string filePath
                                        required property string fileName
                                        required property string fileUrl
                                        required property bool isVideo
                                        required property string extension
                                        required property var filePaths

                                        readonly property string delegateFilePath: filePath
                                        readonly property string delegateFileName: fileName
                                        readonly property string delegateFileUrl: fileUrl
                                        readonly property bool delegateIsVideo: isVideo
                                        readonly property bool selected: filePaths.every(function(path) { return !!root.selectedPathsByFile[path] })
                                        readonly property bool partiallySelected: !selected && filePaths.some(function(path) { return !!root.selectedPathsByFile[path] })
                                        readonly property string delegateExtension: extension
                                        readonly property string delegateFolderLabel: root.pickerFolderLabel(filePath)
                                        objectName: "importMediaCard_" + String(index)

                                        width: mediaGrid.cellWidth - mediaGrid.gutter
                                        height: mediaGrid.cellHeight - mediaGrid.gutter
                                        activeFocusOnTab: true
                                        z: selected ? 1 : 0

                                        function navigate(offset, modifiers) {
                                            const next = Math.max(0, Math.min(root.filteredFileCount - 1, index + offset))
                                            if (modifiers & Qt.ShiftModifier) {
                                                if (root.selectionAnchorPath.length === 0)
                                                    root.selectionAnchorPath = delegateFilePath
                                                root.selectVisibleEntry(next, modifiers)
                                            }
                                            mediaGrid.currentIndex = next
                                            mediaGrid.positionViewAtIndex(next, GridView.Contain)
                                            Qt.callLater(function() {
                                                const card = mediaGrid.itemAtIndex(next)
                                                if (card) card.forceActiveFocus()
                                            })
                                        }
                                        Keys.onReturnPressed: function(event) { root.selectVisibleEntry(index, event.modifiers) }
                                        Keys.onEnterPressed: function(event) { root.selectVisibleEntry(index, event.modifiers) }
                                        Keys.onSpacePressed: root.openPreview(index)
                                        Keys.onLeftPressed: function(event) { navigate(-1, event.modifiers) }
                                        Keys.onRightPressed: function(event) { navigate(1, event.modifiers) }
                                        Keys.onUpPressed: function(event) { navigate(-mediaGrid.columns, event.modifiers) }
                                        Keys.onDownPressed: function(event) { navigate(mediaGrid.columns, event.modifiers) }

                                        ImportImageTile {
                                            objectName: "importMediaTile_" + String(importMediaCard.index)
                                            anchors.fill: parent
                                            activeFocusOnTab: false
                                            Keys.enabled: false
                                            filePath: importMediaCard.delegateFilePath
                                            fileName: importMediaCard.delegateFileName
                                            fileUrl: importMediaCard.delegateFileUrl
                                            isVideo: importMediaCard.delegateIsVideo
                                            extension: importMediaCard.delegateExtension
                                            detailText: ""
                                            selected: importMediaCard.selected
                                            partiallySelected: importMediaCard.partiallySelected
                                            contextualSelection: true
                                            selectionFocus: importMediaCard.activeFocus
                                            onSelectionRequested: function(filePath, modifiers) {
                                                importMediaCard.forceActiveFocus()
                                                root.selectVisibleEntry(importMediaCard.index, modifiers)
                                            }
                                        }
                                    }
                                }

                                Column {
                                    objectName: "importEmptyState"
                                    readonly property string visibilityRole: "contextual|empty-folder-filter-reset"
                                    anchors.centerIn: parent
                                    visible: root.currentFolderPath.length > 0 && root.filteredFileCount === 0
                                    spacing: theme.space10

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 46
                                        height: 46
                                        radius: theme.radius8
                                        color: theme.alpha(theme.ink800, 0.9)
                                        border.width: theme.hairlineWidth
                                        border.color: theme.alpha(theme.lineSoft, 0.2)

                                        AppIcon {
                                            anchors.centerIn: parent
                                            name: "media"
                                            color: theme.textStrong
                                            implicitWidth: 18
                                            implicitHeight: 18
                                        }
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.filenameQuery.length > 0 ? "No matching filenames" : root.mediaQuickFilter === "all" ? "No media" : ("No " + root.mediaFilterLabel().toLowerCase())
                                        color: theme.textStrong
                                        font.family: theme.fontDisplay
                                        font.pixelSize: theme.typePageTitle
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.mediaQuickFilter === "all"
                                              ? "No supported media in this folder. Choose another folder from Sources or select files directly."
                                              : "Try another filter or show all supported media."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeBody
                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        width: Math.min(420, root.width - 96)
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Row {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        visible: root.mediaQuickFilter === "all"
                                        spacing: theme.space8

                                        ActionButton {
                                            text: "Choose Files"
                                            iconName: "media"
                                            compact: true
                                            tone: "secondary"
                                            tooltipText: root.supportedImportTypesText()
                                            onClicked: root.browseForFiles()
                                        }
                                    }

                                    ActionButton {
                                        visible: root.mediaQuickFilter !== "all" || root.filenameQuery.length > 0
                                        iconName: "media"
                                        text: "Show All"
                                        compact: true
                                        onClicked: { root.filenameQuery = ""; root.mediaQuickFilter = "all" }
                                    }
                                }

                                Column {
                                    objectName: "importNoSourceState"
                                    readonly property string visibilityRole: "contextual|no-source-choose-folder-or-files"
                                    visible: !root.hasSource
                                    anchors.centerIn: parent
                                    width: Math.max(1, Math.min(theme.scaled(520), parent.width - theme.space16 * 2))
                                    spacing: theme.space12

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 38
                                        height: 38
                                        radius: theme.radius4
                                        color: theme.alpha(theme.ink800, 0.5)
                                        border.width: 0

                                        AppIcon {
                                            anchors.centerIn: parent
                                            name: "folder"
                                            color: theme.textStrong
                                            implicitWidth: 18
                                            implicitHeight: 18
                                        }
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "Bring your photos in"
                                        color: theme.textStrong
                                        font.family: theme.fontDisplay
                                        font.pixelSize: theme.typePageTitle
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        width: parent.width
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.importMode === "managed"
                                              ? "Drop a folder here or choose one in Sources. Select individual files below."
                                              : "Drop a folder here or choose one in Sources. Originals stay where they are."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeBody
                                        wrapMode: Text.WordWrap
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Row {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        spacing: theme.space8

                                        ActionButton {
                                            objectName: "importNoSourceChooseFolderButton"
                                            text: "Choose Folder"
                                            iconName: "folder"
                                            tone: "primary"
                                            compact: true
                                            onClicked: root.browseForSource()
                                        }

                                        ActionButton {
                                            objectName: "importNoSourceChooseFilesButton"
                                            text: "Choose Files"
                                            iconName: "media"
                                            tone: "secondary"
                                            compact: true
                                            tooltipText: root.supportedImportTypesText()
                                            onClicked: root.browseForFiles()
                                        }
                                    }
                                }
                            }
                    }
                }
            }
        }

            }

        BrandPanel {
            objectName: "importDestinationPanel"
            readonly property string visibilityRole: "contextual|import-preflight-mode-destination-problems-project-confirm-cancel"
            Layout.minimumHeight: 0
            clip: true
            visible: root.importProjectChooserOpen
            Layout.preferredWidth: visible ? parent.width : 0
            Layout.minimumWidth: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Destination"
            subtitle: ""
            accent: theme.accentWarm400
            headerVisible: false

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.importProjectChooserOpen
                spacing: theme.space16

                Rectangle {
                    objectName: "importReviewHero"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.width >= theme.scaled(1000)
                    color: theme.mediaCanvas
                    readonly property string previewPath: {
                        const stagedPhoto = root.pendingImportPaths.find(function(path) { return !root.isVideoFile(path) })
                        if (stagedPhoto) return stagedPhoto
                        if (root.pendingImportKind !== "folder") return ""
                        const firstPhoto = root.filteredFileEntries.find(function(entry) { return !root.isVideoFile(entry.filePath) })
                        return firstPhoto ? firstPhoto.filePath : ""
                    }
                    Image {
                        id: reviewHeroImage
                        anchors.fill: parent
                        anchors.margins: theme.space12
                        source: parent.visible && parent.previewPath.length > 0 ? catalogAssets.originalPreviewSource(parent.previewPath) : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    AppIcon {
                        anchors.centerIn: parent
                        visible: reviewHeroImage.status !== Image.Ready && reviewHeroImage.status !== Image.Loading
                        name: "image"
                        color: theme.textFaint
                        width: theme.scaled(48)
                        height: width
                    }
                    OpticalActivityIndicator {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: theme.space14
                        width: theme.scaled(18)
                        height: width
                        active: reviewHeroImage.status === Image.Loading
                        visible: active
                    }
                }
                ScrollView {
                    id: importPreflightScroll
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: theme.scaled(420)
                    Layout.maximumWidth: root.width >= theme.scaled(1000) ? theme.scaled(460) : theme.scaled(620)
                    Layout.alignment: Qt.AlignHCenter
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    GridLayout {
                        id: projectPickerColumn
                        x: theme.space12
                        width: Math.max(1, importPreflightScroll.availableWidth - (theme.space12 * 2))
                        columns: 1
                        uniformCellWidths: true
                        columnSpacing: theme.space16
                        rowSpacing: theme.space10

                    RowLayout {
                        Layout.row: 0
                        Layout.column: 0
                        Layout.fillWidth: true
                        Layout.columnSpan: 1
                        spacing: theme.space2

                        Label {
                            Layout.fillWidth: true
                            text: "Review & destination"
                            color: theme.textStrong
                            font.family: theme.fontDisplay
                            font.pixelSize: theme.typeTitle
                            font.weight: Font.DemiBold
                        }
                        ActionButton {
                            objectName: "importReviewSelectionToggle"
                            text: root.reviewSelectionExpanded ? "Hide photos" : "Review photos"
                            compact: true
                            visible: root.pendingImportKind === "selection" && root.pendingImportPaths.length > 0
                            onClicked: root.reviewSelectionExpanded = !root.reviewSelectionExpanded
                        }
                    }

                    Label {
                        Layout.row: 1
                        Layout.column: 0
                        Layout.fillWidth: true
                        Layout.columnSpan: 1
                        text: root.pendingImportKind === "folder" ? "Folder import" : root.selectionMixText() + " selected"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    ColumnLayout {
                        objectName: "importReviewBatchStrip"
                        Layout.row: 2
                        Layout.column: 0
                        Layout.columnSpan: 1
                        Layout.fillWidth: true
                        visible: root.reviewSelectionExpanded && root.pendingImportKind === "selection" && root.pendingImportPaths.length > 0
                        spacing: theme.space6
                        implicitHeight: theme.scaled(62)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8
                            Repeater {
                                model: root.pendingImportPaths.slice(0, root.width < theme.scaled(1120) ? 4 : 7)
                                delegate: Rectangle {
                                    id: batchThumbnail
                                    required property string modelData
                                    Layout.preferredWidth: theme.scaled(80)
                                    Layout.preferredHeight: theme.scaled(62)
                                    radius: theme.radius4
                                    color: theme.mediaCanvas
                                    clip: true
                                    Accessible.name: "Preview " + root.fileNameForPath(modelData)
                                    Accessible.role: Accessible.Button
                                    activeFocusOnTab: true
                                    HoverHandler { id: batchHover; cursorShape: Qt.PointingHandCursor }
                                    AppToolTip {
                                        targetItem: batchThumbnail
                                        visible: (batchHover.hovered || batchThumbnail.activeFocus) && !mediaPreviewDialog.opened
                                        text: root.fileNameForPath(batchThumbnail.modelData)
                                    }
                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: theme.space2
                                        source: root.isVideoFile(batchThumbnail.modelData) ? "" : root.fileUrlForPath(batchThumbnail.modelData)
                                        sourceSize.width: theme.scaled(160)
                                        sourceSize.height: theme.scaled(124)
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                    }
                                    AppIcon {
                                        anchors.centerIn: parent
                                        visible: root.isVideoFile(batchThumbnail.modelData)
                                        name: "video"
                                        implicitWidth: theme.scaled(20)
                                        implicitHeight: theme.scaled(20)
                                        color: theme.textMuted
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        color: "transparent"
                                        border.width: batchThumbnail.activeFocus ? theme.focusBorderWidth : 0
                                        border.color: theme.focusRing
                                        radius: theme.radius4
                                    }
                                    Keys.onReturnPressed: root.openReviewPreview(modelData)
                                    Keys.onSpacePressed: root.openReviewPreview(modelData)
                                    TapHandler { onTapped: root.openReviewPreview(batchThumbnail.modelData) }
                                }
                            }
                            Label {
                                visible: root.pendingImportPaths.length > (root.width < theme.scaled(1120) ? 4 : 7)
                                text: "+" + (root.pendingImportPaths.length - (root.width < theme.scaled(1120) ? 4 : 7)) + " more"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }

                    ColumnLayout {
                        objectName: "importProjectTargetSection"
                        Layout.row: 3
                        Layout.column: 0
                        Layout.fillWidth: true
                        spacing: theme.space6

                        Label {
                            text: "Collection"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                        }

                        ToneComboBox {
                            objectName: "importProjectTargetPicker"
                            Layout.fillWidth: true
                            accessibleLabel: "Import destination project"
                            model: root.projectOptionLabels
                            currentIndex: Math.max(0, root.projectOptionValues.indexOf(root.projectTarget))
                            onActivated: function(index) {
                                root.projectTarget = root.projectOptionValues[index]
                            }
                        }

                        ActionButton {
                            objectName: "importNewProjectButton"
                            text: root.newProjectEntryOpen ? "Cancel new project" : "New project…"
                            iconName: "plus"
                            compact: true
                            embedded: true
                            onClicked: {
                                root.newProjectEntryOpen = !root.newProjectEntryOpen
                                if (root.newProjectEntryOpen)
                                    newProjectField.forceActiveFocus()
                            }
                        }

                        RowLayout {
                            visible: root.newProjectEntryOpen
                            Layout.fillWidth: true
                            spacing: theme.space6

                            ToneTextField {
                                id: newProjectField
                                objectName: "importNewProjectField"
                                Layout.fillWidth: true
                                placeholderText: "New project name"
                                onAccepted: {
                                    root.createProjectAndSelect(text)
                                    text = ""
                                }
                            }

                            ActionButton {
                                text: "Create"
                                iconName: "plus"
                                compact: true
                                enabled: newProjectField.text.trim().length > 0
                                onClicked: {
                                    root.createProjectAndSelect(newProjectField.text)
                                    newProjectField.text = ""
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.row: 9
                        Layout.column: 0
                        Layout.fillWidth: true
                        visible: root.workflowProfile === "studio" && root.advancedImportOptionsOpen
                        spacing: theme.space6

                        Label {
                            text: "Import presets"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space6

                            ToneTextField {
                                id: importPresetNameField
                                Layout.fillWidth: true
                                placeholderText: "Preset name"
                                onAccepted: {
                                    if (root.saveCurrentImportPreset(text))
                                        text = ""
                                }
                            }

                            ActionButton {
                                text: "Save"
                                compact: true
                                enabled: importPresetNameField.text.trim().length > 0
                                onClicked: {
                                    if (root.saveCurrentImportPreset(importPresetNameField.text))
                                        importPresetNameField.text = ""
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: appShell.importPresetNames.length === 0
                            text: "Save this setup to reuse its copy, organization, duplicate, RAW + JPEG, and project choices."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: appShell.importPresetNames

                            delegate: RowLayout {
                                id: importPresetRow
                                required property string modelData

                                Layout.fillWidth: true
                                spacing: theme.space4

                                ActionButton {
                                    Layout.fillWidth: true
                                    text: importPresetRow.modelData
                                    iconName: "settings"
                                    onClicked: root.loadImportPreset(importPresetRow.modelData)
                                }

                                ActionButton {
                                    showText: false
                                    iconName: "delete"
                                    compact: true
                                    tooltipText: "Delete " + importPresetRow.modelData
                                    onClicked: {
                                        appShell.removeImportPreset(importPresetRow.modelData)
                                        root.importPresetStatus = "Deleted “" + importPresetRow.modelData + "”."
                                    }
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: root.importPresetStatus.length > 0
                            text: root.importPresetStatus
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            wrapMode: Text.WordWrap
                        }
                    }

                    ColumnLayout {
                        Layout.row: 4
                        Layout.column: 0
                        Layout.fillWidth: true
                        visible: true
                        spacing: theme.space6

                        Label {
                            text: "Original files"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: true
                            spacing: theme.space6

                            ActionButton {
                                objectName: "importManagedModeButton"
                                Layout.fillWidth: true
                                text: "Copy to Library"
                                iconName: "save"
                                active: root.importMode === "managed"
                                onClicked: root.importMode = "managed"
                            }

                            ActionButton {
                                objectName: "importReferencedModeButton"
                                Layout.fillWidth: true
                                text: "Keep in place"
                                iconName: "source"
                                active: root.importMode === "referenced"
                                onClicked: {
                                    root.workflowProfile = "studio"
                                    root.importMode = "referenced"
                                }
                            }
                        }

                        Label {
                            objectName: "importModeOutcomeLabel"
                            Layout.fillWidth: true
                            text: root.importMode === "managed" ? "Originals stay untouched." : "Originals stay in their current location."
                            Accessible.description: root.importPreflight.outcomeText || ""
                            color: theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }

                        Label {
                            objectName: "importFileHandlingSummaryLabel"
                            Layout.fillWidth: true
                            visible: root.advancedImportOptionsOpen
                            text: (root.importMode === "referenced" ? "Link originals" : root.organizationTemplate === "source" ? "Keep source folders" : "Organize by date & camera")
                                  + " · " + (root.duplicatePolicy === "replace" ? "Replace matching files" : "Skip existing files")
                            Accessible.description: root.importAdvancedPolicySummaryText()
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            ActionButton {
                                objectName: "importStudioWorkflowButton"
                                text: "File handling"
                                iconName: "settings"
                                embedded: true
                                compact: true
                                tooltipText: root.importAdvancedPolicySummaryText()
                                onClicked: {
                                    root.workflowProfile = "studio"
                                    root.advancedImportOptionsOpen = !root.advancedImportOptionsOpen
                                }
                            }

                            ActionButton {
                                objectName: "importPersonalWorkflowButton"
                                text: "Use defaults"
                                embedded: true
                                compact: true
                                visible: root.workflowProfile === "studio"
                                onClicked: {
                                    root.workflowProfile = "personal"
                                    root.advancedImportOptionsOpen = false
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.row: 10
                        Layout.column: 0
                        Layout.fillWidth: true
                        visible: root.workflowProfile === "studio" && root.advancedImportOptionsOpen && root.importMode === "managed"
                        spacing: theme.space6

                        Label {
                            text: "Organization"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space6

                            ActionButton {
                                objectName: "importSourceOrganizationButton"
                                Layout.fillWidth: true
                                text: "Source Folders"
                                active: root.organizationTemplate === "source"
                                onClicked: root.organizationTemplate = "source"
                            }

                            ActionButton {
                                objectName: "importDateCameraOrganizationButton"
                                Layout.fillWidth: true
                                text: "Date + Camera"
                                active: root.organizationTemplate === "date-camera"
                                onClicked: root.organizationTemplate = "date-camera"
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.row: 11
                        Layout.column: 0
                        Layout.fillWidth: true
                        visible: root.workflowProfile === "studio" && root.advancedImportOptionsOpen
                        spacing: theme.space6

                        Label {
                            text: "RAW + JPEG pairs"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space4

                            ActionButton {
                                objectName: "importBothPairPolicyButton"
                                Layout.fillWidth: true
                                text: "Both"
                                active: root.rawJpegPolicy === "both"
                                onClicked: root.rawJpegPolicy = "both"
                            }

                            ActionButton {
                                objectName: "importRawOnlyPairPolicyButton"
                                Layout.fillWidth: true
                                text: "RAW Only"
                                active: root.rawJpegPolicy === "raw-only"
                                onClicked: root.rawJpegPolicy = "raw-only"
                            }

                            ActionButton {
                                objectName: "importJpegOnlyPairPolicyButton"
                                Layout.fillWidth: true
                                text: "JPEG Only"
                                active: root.rawJpegPolicy === "jpeg-only"
                                onClicked: root.rawJpegPolicy = "jpeg-only"
                            }
                        }
                    }

                    ColumnLayout {
                        objectName: "importAdvancedFilePolicySection"
                        Layout.row: 12
                        Layout.column: 0
                        Layout.columnSpan: 1
                        Layout.fillWidth: true
                        visible: root.workflowProfile === "studio" && root.advancedImportOptionsOpen
                        spacing: theme.space6

                        Label {
                            objectName: "importFilenamePolicyLabel"
                            Layout.fillWidth: true
                            text: "Keep original filenames"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.importMode === "managed"
                                  ? (appShell.originalsBackupEnabled
                                     ? "Originals backup · On · " + root.fileNameForPath(appShell.originalsBackupLocation)
                                     : "Originals backup · Off")
                                  : "Originals backup is used with Managed Copy; originals stay in place here."
                            color: appShell.originalsBackupEnabled && root.importMode === "managed"
                                   ? theme.textMuted : theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            Accessible.description: appShell.originalsBackupEnabled
                                                     ? appShell.originalsBackupLocation
                                                     : "An additional originals copy is not configured."
                        }

                        ActionButton {
                            objectName: "importBackupOptionsButton"
                            compact: true
                            text: "Backup options…"
                            tooltipText: "Choose an originals backup folder or turn off the backup."
                            enabled: !appShell.photoLibraryMigrationRunning && !root.hasActiveImportJobs()
                            disabledReason: "Wait for import activity and Photo Library migration to finish before changing backup settings."
                            onClicked: importStorageDialog.open()
                        }
                    }

                    Rectangle {
                        objectName: "importPreflightFacts"
                        Layout.row: 5
                        Layout.column: 0
                        Layout.fillWidth: true
                        Layout.columnSpan: 1
                        implicitHeight: preflightFactsColumn.implicitHeight + (theme.space10 * 2)
                        radius: 0
                        color: "transparent"
                        border.width: 0

                        GridLayout {
                            id: preflightFactsColumn
                            anchors.fill: parent
                            anchors.margins: theme.space10
                            columns: 1
                            uniformCellWidths: true
                            columnSpacing: theme.space16
                            rowSpacing: theme.space12

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.columnSpan: 1
                                spacing: theme.space8
                            Label {
                                objectName: "importPreflightReadiness"
                                Layout.fillWidth: true
                                text: catalogAssets.importPreflightBusy
                                      ? "Checking files… " + catalogAssets.importPreflightScannedCount + " scanned"
                                      : root.importPreflight.ok
                                        ? (root.importPreflight.importableCount || 0) + " files ready to import"
                                        : root.importPreflightBlockerCount > 0 ? "Resolve issues before importing" : "No importable media"
                                color: root.importPreflightBlockerCount > 0 ? theme.error : theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeBody
                                font.weight: Font.DemiBold
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                                ActionButton {
                                    objectName: "importReviewDuplicatesButton"
                                    text: "Compare matches…"
                                    visible: (root.importPreflight.duplicateCount || 0) > 0
                                    embedded: true
                                    compact: true
                                    enabled: root.duplicateReviewMatches.length > 0
                                    onClicked: { root.duplicateReviewIndex = 0; duplicateReviewDialog.open() }
                                }
                                ActionButton {
                                    objectName: "importReviewPathsButton"
                                    text: root.reviewPathsExpanded ? "Less" : "Details"
                                    compact: true
                                    embedded: true
                                    onClicked: root.reviewPathsExpanded = !root.reviewPathsExpanded
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.columnSpan: 1
                                visible: root.preflightExceptionsText().length > 0
                                text: root.preflightExceptionsText()
                                color: root.importPreflightBlockerCount > 0 ? theme.error : theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            ReviewLocation {
                                visible: root.reviewPathsExpanded
                                caption: "Source"
                                path: root.importPreflight.sourcePath || root.pendingImportRootPath
                                summary: path === "/" ? "Computer" : ""
                            }

                            ReviewLocation {
                                visible: root.reviewPathsExpanded
                                caption: root.importMode === "referenced" ? "Storage" : "Destination"
                                path: root.importPreflight.destinationPath || ""
                                summary: root.importMode === "referenced" ? "Files stay in their current location" : "Photo Library"
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: root.reviewPathsExpanded || !!root.importPreflight.backupEnabled
                                text: root.importPreflight.backupEnabled
                                      ? "Second copy · " + (root.reviewPathsExpanded
                                          ? (root.importPreflight.backupDestinationPath || "Checking…")
                                          : (root.fileNameForPath(root.importPreflight.backupDestinationPath || "") || "Checking…"))
                                      : "Second copy · Not configured"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                Accessible.description: root.importPreflight.backupDestinationPath || ""
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: root.reviewPathsExpanded || !!root.importPreflight.backupEnabled
                                text: root.importPreflight.backupEnabled
                                      ? "Primary " + (root.importPreflight.primaryEstimatedDiskText || "0 B") +
                                        " · Backup " + (root.importPreflight.backupEstimatedDiskText || "0 B") +
                                        " · Verified by " + (root.importPreflight.copyVerification || "SHA-256")
                                      : "Primary copy " + (root.importPreflight.primaryEstimatedDiskText || "0 B")
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WordWrap
                            }


                            Label {
                                Layout.fillWidth: true
                                visible: root.pendingImportKind === "folder"
                                text: [
                                    (root.importPreflight.photoCount || 0) > 0
                                        ? root.importPreflight.photoCount + (root.importPreflight.photoCount === 1 ? " photo" : " photos") : "",
                                    (root.importPreflight.videoCount || 0) > 0
                                        ? root.importPreflight.videoCount + (root.importPreflight.videoCount === 1 ? " video" : " videos") : "",
                                    (root.importPreflight.audioCount || 0) > 0
                                        ? root.importPreflight.audioCount + (root.importPreflight.audioCount === 1 ? " audio file" : " audio files") : ""
                                ].filter(function(part) { return part.length > 0 }).join(" · ")
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.importMode === "referenced"
                                      ? (root.importPreflight.selectedSizeText || "0 B") + " selected · No file copy required"
                                      : root.reviewPathsExpanded ? "Selected " + (root.importPreflight.selectedSizeText || "0 B") +
                                        " · Additional disk " + (root.importPreflight.estimatedDiskText || "0 B")
                                      : (root.importPreflight.estimatedDiskText || "0 B") + " disk space required"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: (root.importPreflight.rawJpegPairCount || 0) > 0
                                text: "RAW + JPEG pairs · " + (root.importPreflight.rawJpegPairCount || 0)
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.columnSpan: 1
                                visible: root.importMode === "managed" && root.advancedImportOptionsOpen
                                text: root.importPreflight.organizationText || "Keep source folders and original filenames."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.row: 7
                        Layout.column: 0
                        Layout.fillWidth: true
                        visible: (root.importPreflight.duplicateCount || 0) > 0
                        spacing: theme.space6

                        Label {
                            text: "Duplicates"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space6

                            ActionButton {
                                objectName: "importDuplicateSkipButton"
                                Layout.fillWidth: true
                                text: "Skip Existing"
                                active: root.duplicatePolicy === "skip"
                                onClicked: root.duplicatePolicy = "skip"
                            }

                            ActionButton {
                                objectName: "importDuplicateReplaceButton"
                                Layout.fillWidth: true
                                text: "Replace Existing"
                                active: root.duplicatePolicy === "replace"
                                onClicked: root.duplicatePolicy = "replace"
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.row: 8
                        Layout.column: 0
                        Layout.fillWidth: true
                        Layout.columnSpan: 1
                        visible: (root.importPreflight.issues || []).length > 0
                        spacing: theme.space6

                        Label {
                            text: "Review issues"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: root.importPreflight.issues || []

                            delegate: Rectangle {
                                required property var modelData

                                Layout.fillWidth: true
                                implicitHeight: importIssueColumn.implicitHeight + (theme.space8 * 2)
                                radius: theme.radius4
                                color: modelData.severity === "error"
                                       ? theme.alpha(theme.error, 0.1)
                                       : theme.alpha(theme.warning, 0.08)
                                border.width: theme.hairlineWidth
                                border.color: modelData.severity === "error"
                                              ? theme.alpha(theme.error, 0.28)
                                              : theme.alpha(theme.warning, 0.24)

                                ColumnLayout {
                                    id: importIssueColumn
                                    anchors.fill: parent
                                    anchors.margins: theme.space8
                                    spacing: theme.space2

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.title || "Import issue"
                                        color: theme.textStrong
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeMicro
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.detail || ""
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeMicro
                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
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

        GridLayout {
            id: importPersistentActionBar
            objectName: "importPersistentActionBar"
            readonly property string visibilityRole: "always-visible|destination-change-backup-status-one-primary-import-selected-count"
            Layout.fillWidth: true
            columns: root.width < theme.scaled(600) ? 1 : 2
            columnSpacing: theme.space12
            rowSpacing: theme.space8

            ColumnLayout {
                objectName: "importDestinationSummary"
                Layout.row: 0
                Layout.column: 0
                Layout.fillWidth: true
                spacing: theme.space2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space6

                    Label {
                        text: root.importMode === "referenced" ? "Storage" : "Destination"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: Font.DemiBold
                    }

                    Label {
                        id: importDestinationNameLabel
                        objectName: "importDestinationNameLabel"
                        Layout.fillWidth: true
                        text: root.importMode === "referenced" ? "Keep originals in place" : "Photo Library"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: Font.Medium
                        wrapMode: Text.NoWrap
                        elide: Text.ElideMiddle
                        Accessible.name: "Photo Library destination"
                        Accessible.description: appShell.photoLibraryLocation

                        HoverHandler { id: importDestinationHover }
                        AppToolTip {
                            targetItem: importDestinationNameLabel
                            visible: importDestinationHover.hovered && appShell.photoLibraryLocation.length > 0
                            text: appShell.photoLibraryLocation
                        }
                    }
                }

                Label {
                    objectName: "importBackupDestinationSummaryLabel"
                    Layout.fillWidth: true
                    visible: appShell.originalsBackupEnabled && root.importMode === "managed"
                    text: root.importMode === "referenced" ? "No files copied" : "Backup · " + (appShell.originalsBackupEnabled
                          ? (root.fileNameForPath(appShell.originalsBackupLocation) || "Configured")
                          : "Off")
                    color: appShell.originalsBackupEnabled ? theme.textMuted : theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    Accessible.description: appShell.originalsBackupEnabled
                                             ? appShell.originalsBackupLocation
                                             : "An additional originals copy is not configured."
                }
            }

            RowLayout {
                objectName: "importPersistentActionControls"
                Layout.row: root.width < theme.scaled(600) ? 1 : 0
                Layout.column: root.width < theme.scaled(600) ? 0 : 1
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: theme.space8

                ActionButton {
                    objectName: "importDestinationChangeButton"
                    compact: true
                    text: "Storage…"
                    tooltipText: "Choose the Photo Library or originals backup folder."
                    enabled: !appShell.photoLibraryMigrationRunning && !root.hasActiveImportJobs()
                    disabledReason: "Wait for import activity and Photo Library migration to finish before changing storage."
                    onClicked: importStorageDialog.open()
                }

                ActionButton {
                    objectName: "importReviewCancelButton"
                    compact: true
                    visible: root.importProjectChooserOpen
                    text: "Cancel"
                    tooltipText: "Return to the source and photo selection."
                    onClicked: root.importProjectChooserOpen = false
                }

                ActionButton {
                    objectName: "importPersistentPrimaryButton"
                    Layout.minimumWidth: theme.scaled(204)
                    text: root.persistentImportActionText()
                    accessibleName: root.persistentImportActionText()
                    iconName: "save"
                    tone: "primary"
                    enabled: root.canActivatePersistentImport
                    disabledReason: root.persistentImportDisabledReason()
                    onClicked: root.activatePersistentImportAction()
                }
            }
    }
}
}
