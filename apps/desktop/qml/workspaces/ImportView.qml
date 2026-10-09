pragma ComponentBehavior: Bound
import QtQuick
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

    readonly property string importVisibilityContract: "import-visibility:v1|always:sources-folders-header-media-grid-primary-import|contextual:destination-panel-selection-actions-filter-empty-progress|one-expansion:import-options-project-target-refresh-sources-clear-selection|hover-focus:media-selection-source-folder-keyboard|diagnostic:import-progress-source-empty|preserve:functionality"
    readonly property string importSurfaceContract: "import-surfaces:v1|sources:left-rail|folders:navigation-list|media:grid-filter-selection|destination:contextual-project-picker|actions:split-import-menu|no-elide:true|no-overlap:true"
    readonly property string importWorkflowPolishContract: "import-workflow-polish:v1|first-glance:sources-current-folder-media-grid-primary-import|close-inspection:source-confirmation-filter-counts-selection-destination-progress|clutter:single-progress-place-split-import-options|interaction:choose-source-select-visible-clear-import-retry-refresh|text:no-elide-no-overlap-paths-wrap|functionality:preserved"
    readonly property string importPageCompletionContract: "page-completion:v1|import|sidebar:sources-folders-refresh|content:header-filter-media-grid-empty|destination:project-picker-create-confirm-cancel|toolbar:select-visible-clear-primary-import|states:no-source-empty-filtered-selected-importing-failed-complete"
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
    property bool importProjectChooserOpen: false
    property string importMode: "managed"
    property string duplicatePolicy: "skip"
    property string pendingImportKind: "selection"
    property string pendingImportRootPath: ""
    property var pendingImportPaths: []
    readonly property var importPreflight: catalogAssets.importPreflightResult
    property string mediaQuickFilter: "all"
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
    readonly property bool canConfirmImport: !!importPreflight.ok && !catalogAssets.importPreflightBusy && !catalogAssets.busy
    readonly property string importActionContract: "import-actions:v1|split-button:true|primary:import-to-photo-library|menu:import-selection,import-folder,import-with-preset,select-visible,clear-selection,refresh-sources|disabled-reasons:true|no-clutter:true"
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
        selectionRevision
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
            entries.push({
                           filePath: filePath,
                           fileName: fileName,
                           fileUrl: pickerSelection ? root.fileUrlForPath(filePath) : fileModel.get(index, "fileUrl"),
                           isVideo: isVideo,
                           extension: fileName.indexOf(".") >= 0
                                      ? fileName.split(".").slice(-1)[0].toUpperCase()
                                      : "FILE"
                       })
        }
        return entries
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
        const labels = ["No Project"]
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
        for (let index = 0; index < filteredFileEntries.length; ++index)
            nextSelection[filteredFileEntries[index].filePath] = true
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
        pendingImportRootPath = currentFolderPath
        pendingImportPaths = []
        importProjectChooserOpen = true
        refreshImportPreflight()
    }

    function importActionMenuItems() {
        return [
            {
                key: "import-selection",
                label: "Import Selection",
                icon: "media",
                enabled: root.canImportSelection,
                disabledReason: root.selectedFileCount > 0 ? "Selected files are not importable." : "Select media to import.",
                tooltip: "Import the currently selected files."
            },
            {
                key: "import-folder",
                label: "Import Current Folder",
                icon: "folder",
                enabled: root.canImportFolder,
                disabledReason: root.sourceRootIsFileSelection
                                ? "Choose a source folder to import its contents; this picker contains only the selected files."
                                : (root.currentFolderPath.length > 0 ? "No importable media found in this folder." : "Choose a folder first."),
                tooltip: "Import the current folder."
            },
            {
                key: "import-with-preset",
                label: "Import With Preset",
                icon: "swatch",
                enabled: root.canImportSelection || root.canImportFolder,
                disabledReason: "Select media or choose a folder first.",
                tooltip: "Open Studio preflight and load a saved import preset."
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
        if (key === "import-selection") {
            importSelected()
        } else if (key === "import-folder") {
            importCurrentFolder()
        } else if (key === "import-with-preset") {
            workflowProfile = "studio"
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
        return activeImportJob.displayLabel && activeImportJob.displayLabel.length > 0
                ? "Importing " + activeImportJob.displayLabel
                : "Importing media"
    }

    function importActivityProgress() {
        if (!hasActiveImportJobs())
            return -1
        const status = activeImportJob.status ? String(activeImportJob.status) : ""
        const raw = activeImportJob.progress !== undefined ? Number(activeImportJob.progress) : 0
        if (status === "Queued")
            return 0.08
        if (raw > 0)
            return Math.max(0.05, Math.min(1, raw / 100.0))
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
        return root.importJobStatus()
    }

    function importWorkflowPolishSignature() {
        const hasJob = root.hasActiveImportJobs()
        return "import-workflow-polish:v1" +
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
        folder: root.currentFolderUrl
        showDirs: true
        showFiles: false
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        sortCaseSensitive: false
    }

    FolderListModel {
        id: fileModel
        folder: root.currentFolderUrl
        showDirs: false
        showFiles: true
        showDotAndDotDot: false
        sortField: FolderListModel.Name
        sortCaseSensitive: false
        nameFilters: catalogAssets.importFileNameFilters
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: theme.space12

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: theme.space12

            BrandPanel {
                objectName: "importSourcePanel"
                readonly property string visibilityRole: "always-visible|source-list-folder-nav-folder-list-refresh"
                visible: !root.importProjectChooserOpen || root.width >= theme.scaled(1120)
                Layout.preferredWidth: root.width < theme.scaled(1120)
                                      ? theme.scaled(208)
                                      : Math.min(theme.scaled(320), Math.max(theme.scaled(240), root.width * 0.2))
                Layout.minimumWidth: theme.scaled(192)
                Layout.fillHeight: true
                title: "Sources"
                subtitle: ""
                accent: theme.accentCool400

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                ActionButton {
                    text: "Files"
                    iconName: "media"
                    compact: true
                    tooltipText: "Choose one or many individual media files"
                    onClicked: root.browseForFiles()
                }

                ActionButton {
                    text: "Folder"
                    iconName: "folder"
                    compact: true
                    tooltipText: "Choose any local folder or connected volume"
                    onClicked: root.browseForSource()
                }

                Item { Layout.fillWidth: true }

                ActionButton {
                    showText: false
                    iconName: "source"
                    tooltipText: "Refresh sources"
                    enabled: !catalogAssets.busy
                    onClicked: catalogAssets.refreshImportSources(true)
                }
            }

            ListView {
                id: sourceListView
                objectName: "importSourceList"
                readonly property string visibilityRole: "always-visible|mounted-sources-select"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 250)
                Layout.fillHeight: false
                clip: true
                spacing: theme.space4
                model: catalogAssets.importSources
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
                            root.openSource(path, sourceUrl, label)
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
                visible: root.hasSource
                Layout.fillWidth: true
                text: "FOLDERS"
                color: theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                font.weight: Font.DemiBold
            }

            Rectangle {
                objectName: "importFolderNavigation"
                readonly property string visibilityRole: "contextual|up-current-folder-source-folder-count"
                visible: root.hasSource
                Layout.fillWidth: true
                implicitHeight: folderNavRow.implicitHeight + (theme.space8 * 2)
                radius: theme.radius8
                color: theme.alpha(theme.ink950, 0.18)
                border.width: theme.hairlineWidth
                border.color: theme.alpha(theme.lineSoft, 0.12)

                RowLayout {
                    id: folderNavRow
                    anchors.fill: parent
                    anchors.margins: theme.space8
                    spacing: theme.space8

                    ActionButton {
                        iconName: "chevron-left"
                        showText: false
                        tooltipText: "Up one folder"
                        compact: true
                        embedded: true
                        enabled: root.canGoUp
                        onClicked: root.goUp()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: theme.space2

                        Label {
                            Layout.fillWidth: true
                            text: root.currentFolderName
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.Medium
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: theme.typeMicro
                            elide: Text.ElideNone
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.sourceLabel
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: theme.typeMicro
                            elide: Text.ElideNone
                        }
                    }

                    Rectangle {
                        implicitWidth: folderCountLabel.implicitWidth + (theme.space8 * 2)
                        implicitHeight: folderCountLabel.implicitHeight + theme.space6
                        radius: theme.radiusChip
                        color: theme.alpha(theme.ink800, 0.92)
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.16)

                        Label {
                            id: folderCountLabel
                            anchors.centerIn: parent
                            text: String(directoryModel.count)
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeMicro
                        }
                    }
                }
            }

            ListView {
                id: folderListView
                objectName: "importFolderList"
                readonly property string visibilityRole: "contextual|folder-browse-keyboard"
                visible: root.hasSource
                Layout.fillWidth: true
                Layout.fillHeight: true
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

        }

            Rectangle {
                objectName: "importMainPanel"
                readonly property string surfaceContract: "import-main-panel:v1|header:always|media-grid:always|filter:always|selection-actions:always|empty-states:contextual|primary-import:always|no-elide:true|no-overlap:true"
                visible: !root.importProjectChooserOpen || root.width >= theme.scaled(1120)
                Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radiusSurface
            color: theme.ink850
            border.width: theme.hairlineWidth
            border.color: theme.alpha(theme.lineSoft, 0.18)

            gradient: Gradient {
                GradientStop { position: 0.0; color: theme.alpha(theme.accentCool500, 0.05) }
                GradientStop { position: 0.14; color: theme.alpha(theme.ink850, 0.98) }
                GradientStop { position: 1.0; color: theme.ink900 }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.width < theme.scaled(1120) ? theme.panelPadding : theme.panelPaddingStandard
                spacing: theme.space12

                Rectangle {
                    objectName: "importHeaderBar"
                    readonly property string visibilityRole: "always-visible|source-path-counts-select-visible-clear-import"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(theme.scaled(96), importHeaderContent.implicitHeight + theme.space20)
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink900, 0.92)
                    border.width: theme.hairlineWidth
                    border.color: theme.alpha(theme.lineSoft, 0.3)

                    GridLayout {
                        id: importHeaderContent
                        anchors.fill: parent
                        anchors.margins: theme.space16
                        columns: root.width < theme.scaled(1120) ? 1 : 2
                        columnSpacing: theme.space12
                        rowSpacing: theme.space8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: theme.space4

                            Label {
                                Layout.fillWidth: true
                                text: root.hasSource ? root.sourceLabel : "Pick a source"
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: theme.typePageTitle
                                font.weight: Font.DemiBold
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: 1
                                elide: Text.ElideNone
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.currentFolderPath.length > 0 ? root.currentFolderPath
                                      : root.pendingFolderPath.length > 0 ? "Opening folder…"
                                      : root.hasSource ? "Choose a folder in this source"
                                      : "Choose a mounted drive or folder"
                                color: root.currentFolderPath.length > 0 ? theme.accentWarm300 : theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: root.width < theme.scaled(1120) ? 2 : 1
                                elide: Text.ElideNone
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space8

                                StatusChip {
                                    objectName: "importSourceConfirmationChip"
                                    text: root.sourceConfirmationText()
                                    iconName: root.hasSource ? "check" : "source"
                                    tone: root.hasSource ? "success" : "neutral"
                                    compact: true
                                    minimumReadableWidth: 104
                                    maximumReadableWidth: 154
                                    tooltipText: root.currentFolderPath.length > 0
                                                 ? root.currentFolderPath
                                                 : "Choose an import source from the left rail."
                                }

                                StatusChip {
                                    objectName: "importSelectionMixChip"
                                    text: root.selectionMixText()
                                    iconName: root.selectedFileCount > 0 ? "media" : "grid"
                                    tone: root.selectedFileCount > 0
                                          ? (root.selectedPhotoCount > 0 && root.selectedVideoCount > 0 ? "warning" : "accent")
                                          : "neutral"
                                    compact: true
                                    minimumReadableWidth: 104
                                    maximumReadableWidth: 232
                                    tooltipText: root.selectedFileCount > 0
                                                 ? "Selection includes " + root.selectedPhotoCount + " photos and " + root.selectedVideoCount + " videos across " + root.selectedFolderCount + (root.selectedFolderCount === 1 ? " folder." : " folders.")
                                                 : root.filteredFileCount + " visible files in this folder."
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignRight
                            spacing: theme.space8

                            Item { Layout.fillWidth: true; visible: root.width < theme.scaled(1120) }

                            ActionButton {
                                iconName: "plus"
                                text: "Select all"
                                compact: true
                                enabled: root.filteredFileCount > 0
                                onClicked: root.selectAllVisible()
                            }

                            ActionButton {
                                iconName: "delete"
                                showText: false
                                tooltipText: "Clear selection"
                                compact: true
                                enabled: root.selectedFileCount > 0
                                onClicked: root.clearSelection()
                            }

                            SplitButton {
                                objectName: "importPrimarySplitButton"
                                readonly property string visibilityRole: "always-visible|primary-import-one-expansion-options"
                                text: root.hasActiveImportJobs() ? "Importing" : "Import to Photo Library"
                                iconName: "media"
                                tone: "primary"
                                primaryEnabled: root.canImportSelection
                                disabledReason: root.hasStagedSelection
                                                ? "Selected files are not importable right now."
                                                : "Select media to import."
                                menuItems: root.importActionMenuItems()
                                menuTooltipText: "Import options"
                                onClicked: root.importSelected()
                                onMenuItemTriggered: function(key) { root.handleImportMenuAction(key) }
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "importProgressSummaryCard"
                    readonly property string visibilityRole: "contextual|queued-running-canceling-completed-failed-canceled-summary-actions"
                    Layout.fillWidth: true
                    visible: !!root.activeImportJob.jobId
                    implicitHeight: importProgressSummaryRow.implicitHeight + (theme.space8 * 2)
                    radius: theme.radius10
                    color: theme.alpha(theme.ink950, 0.32)
                    border.width: theme.hairlineWidth
                    border.color: root.activeImportJob.status === "Failed"
                                  ? theme.alpha(theme.error, 0.32)
                                  : theme.alpha(theme.lineSoft, 0.18)

                    RowLayout {
                        id: importProgressSummaryRow
                        anchors.fill: parent
                        anchors.margins: theme.space8
                        spacing: theme.space8

                        StatusChip {
                            text: root.activeImportJob.status || "Queued"
                            iconName: root.activeImportJob.status === "Completed" ? "check" : "media"
                            tone: root.activeImportJob.status === "Completed"
                                  ? "success"
                                  : (root.activeImportJob.status === "Failed" ? "warning" : "accent")
                            compact: true
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
                                Layout.fillWidth: true
                                visible: root.activeImportJob.status === "Completed"
                                text: root.importBackupReceiptText()
                                color: root.importSummaryMetadata().backupEnabled ? theme.success : theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAnywhere
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

                            LooksyLoadingScene {
                                objectName: "importLoadingScene"
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Math.min(280, parent.width)
                                visible: root.hasActiveImportJobs()
                                scene: "import"
                                compact: true
                                active: root.hasActiveImportJobs()
                                progress: root.importActivityProgress()
                            }
                        }

                        ActionButton {
                            visible: jobQueue && jobQueue.canPauseJob(root.activeImportJob.jobId || "")
                            text: "Pause"
                            iconName: "media"
                            compact: true
                            onClicked: jobQueue.pauseJob(root.activeImportJob.jobId)
                        }

                        ActionButton {
                            visible: jobQueue && jobQueue.canResumeJob(root.activeImportJob.jobId || "")
                            text: "Resume"
                            iconName: "media"
                            compact: true
                            onClicked: jobQueue.resumeJob(root.activeImportJob.jobId)
                        }

                        ActionButton {
                            visible: jobQueue && jobQueue.canCancelJob(root.activeImportJob.jobId || "")
                            text: "Cancel"
                            iconName: "delete"
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
                    spacing: theme.space10

                    BrandPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        title: ""
                        subtitle: ""
                        accent: theme.accentCool400
                        headerVisible: false

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: theme.space6

                            Rectangle {
                                objectName: "importMediaFilterBar"
                                readonly property string visibilityRole: "always-visible|all-photos-videos-selected-filter-summary-reset"
                                Layout.fillWidth: true
                                implicitHeight: mediaToolbar.implicitHeight + (theme.space6 * 2)
                                radius: theme.radius10
                                color: theme.alpha(theme.ink950, 0.18)
                                border.width: theme.hairlineWidth
                                border.color: theme.alpha(theme.lineSoft, 0.12)

                                ColumnLayout {
                                    id: mediaToolbar
                                    anchors.fill: parent
                                    anchors.margins: theme.space6
                                    spacing: 0

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: theme.space6

                                        FilterChip {
                                            text: "All"
                                            accent: theme.accentWarm400
                                            active: root.mediaQuickFilter === "all"
                                            onClicked: root.mediaQuickFilter = "all"
                                        }

                                        FilterChip {
                                            text: "Photos"
                                            accent: theme.accentCool400
                                            active: root.mediaQuickFilter === "photos"
                                            onClicked: root.mediaQuickFilter = "photos"
                                        }

                                        FilterChip {
                                            text: "Videos"
                                            accent: theme.warning
                                            active: root.mediaQuickFilter === "videos"
                                            onClicked: root.mediaQuickFilter = "videos"
                                        }

                                        FilterChip {
                                            text: "Selected"
                                            accent: theme.success
                                            active: root.mediaQuickFilter === "selected"
                                            onClicked: root.mediaQuickFilter = "selected"
                                        }

                                        Item { Layout.fillWidth: true }

                                        Label {
                                            text: root.mediaFilterLabel() + " · " + root.filteredFileCount + " shown"
                                            color: theme.textFaint
                                            font.family: theme.fontMono
                                            font.pixelSize: theme.typeMicro
                                        }

                                        ActionButton {
                                            iconName: "delete"
                                            showText: false
                                            tooltipText: "Reset filter"
                                            compact: true
                                            visible: root.mediaQuickFilter !== "all"
                                            onClicked: root.mediaQuickFilter = "all"
                                        }
                                    }
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                GridView {
                                    id: mediaGrid
                                    objectName: "importMediaGrid"
                                    readonly property string visibilityRole: "always-visible|media-preview-selection-keyboard"
                                    anchors.fill: parent
                                    clip: true
                                    property int columns: Math.max(2, Math.floor(width / 210))
                                    property int gutter: theme.space8
                                    readonly property bool compactPreview: root.height < theme.scaled(760)
                                    readonly property real compactPreviewAspectRatio: compactPreview ? 0.54 : 0.68
                                    readonly property int compactPreviewMinimumHeight: compactPreview ? 110 : 142
                                    cellWidth: Math.floor((width - ((columns - 1) * gutter)) / columns)
                                    cellHeight: Math.max(compactPreviewMinimumHeight,
                                                         (cellWidth - gutter) * compactPreviewAspectRatio) + 48
                                    boundsBehavior: Flickable.StopAtBounds
                                    model: root.filteredFileEntries

                                    delegate: FocusScope {
                                        id: importMediaCard
                                        required property int index
                                        required property string filePath
                                        required property string fileName
                                        required property string fileUrl
                                        required property bool isVideo
                                        required property string extension

                                        readonly property string delegateFilePath: filePath
                                        readonly property string delegateFileName: fileName
                                        readonly property string delegateFileUrl: fileUrl
                                        readonly property bool delegateIsVideo: isVideo
                                        readonly property bool selected: !!root.selectedPathsByFile[filePath]
                                        readonly property string delegateExtension: extension
                                        readonly property string delegateFolderLabel: root.pickerFolderLabel(filePath)
                                        objectName: "importMediaCard_" + String(index)

                                        width: mediaGrid.cellWidth - mediaGrid.gutter
                                        height: previewFrame.height + infoColumn.implicitHeight + theme.space6
                                        activeFocusOnTab: true
                                        scale: selected ? 1.01 : (mediaMouseArea.containsMouse ? 1.008 : 1.0)
                                        z: selected || mediaMouseArea.containsMouse ? 1 : 0

                                        Behavior on scale {
                                            NumberAnimation { duration: theme.durationFast }
                                        }

                                        Keys.onReturnPressed: root.toggleSelection(delegateFilePath)
                                        Keys.onEnterPressed: root.toggleSelection(delegateFilePath)
                                        Keys.onSpacePressed: root.toggleSelection(delegateFilePath)

                                        Rectangle {
                                            id: previewFrame
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            readonly property real cardAspectRatio: mediaGrid.compactPreviewAspectRatio
                                            height: Math.max(mediaGrid.compactPreviewMinimumHeight, width * cardAspectRatio)
                                            radius: theme.radius12
                                            color: selected
                                                   ? theme.alpha(theme.accentWarm500, 0.12)
                                                   : (mediaMouseArea.containsMouse ? theme.alpha(theme.ink800, 0.92) : theme.alpha(theme.ink900, 0.84))
                                            clip: true
                                            border.width: selected || importMediaCard.activeFocus ? 2 : 1
                                            border.color: importMediaCard.activeFocus
                                                          ? theme.alpha(theme.focusRing, 0.92)
                                                          : selected
                                                            ? theme.alpha(theme.accentWarm300, 0.9)
                                                            : (mediaMouseArea.containsMouse ? theme.alpha(theme.lineStrong, 0.34) : theme.alpha(theme.lineSoft, 0.16))

                                            Behavior on color {
                                                ColorAnimation { duration: theme.durationFast }
                                            }

                                            Behavior on border.color {
                                                ColorAnimation { duration: theme.durationFast }
                                            }

                                            Image {
                                                id: previewImage
                                                anchors.fill: parent
                                                source: delegateIsVideo ? "" : delegateFileUrl
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                                cache: true
                                                sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                                                sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                                                visible: !delegateIsVideo && status === Image.Ready
                                            }

                                            Rectangle {
                                                anchors.fill: parent
                                                gradient: Gradient {
                                                    GradientStop { position: 0.0; color: selected ? theme.alpha(theme.accentWarm400, 0.06) : theme.alpha(theme.accentCool500, 0.03) }
                                                    GradientStop { position: 1.0; color: theme.alpha(theme.ink950, 0.14) }
                                                }
                                            }

                                            Rectangle {
                                                anchors.fill: parent
                                                color: theme.alpha(theme.ink950, mediaMouseArea.containsMouse ? 0.06 : (selected ? 0.04 : 0.0))
                                            }

                                            Rectangle {
                                                anchors.fill: parent
                                                visible: importMediaCard.activeFocus
                                                color: theme.alpha(theme.focusFill, 0.14)
                                            }

                                            Rectangle {
                                                anchors.fill: parent
                                                visible: delegateIsVideo || previewImage.status !== Image.Ready
                                                color: theme.alpha(theme.ink900, 0.88)

                                                Column {
                                                    anchors.centerIn: parent
                                                    spacing: theme.space4

                                                    Label {
                                                        anchors.horizontalCenter: parent.horizontalCenter
                                                        text: delegateIsVideo ? "VIDEO" : delegateExtension
                                                        color: theme.accentWarm300
                                                        font.family: theme.fontMono
                                                        font.pixelSize: theme.typeSection
                                                        font.weight: Font.DemiBold
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                anchors.top: parent.top
                                                anchors.right: parent.right
                                                anchors.margins: theme.space8
                                                width: 24
                                                height: 24
                                                radius: theme.radius6
                                                color: selected ? theme.accentWarm400 : theme.alpha(theme.ink900, 0.72)
                                                border.width: theme.hairlineWidth
                                                border.color: selected
                                                              ? theme.alpha(theme.accentWarm300, 0.92)
                                                              : theme.alpha(theme.lineStrong, 0.42)

                                                AppIcon {
                                                    anchors.centerIn: parent
                                                    name: selected ? "check" : "plus"
                                                    color: selected ? theme.ink950 : theme.textMain
                                                    width: 14
                                                    height: width
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            id: infoColumn
                                            anchors.top: previewFrame.bottom
                                            anchors.topMargin: theme.space4
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            spacing: theme.space2

                                            Label {
                                                Layout.fillWidth: true
                                                text: delegateFileName
                                                color: theme.textStrong
                                                font.family: theme.fontUi
                                                font.pixelSize: theme.typeCaption
                                                font.weight: Font.Medium
                                                fontSizeMode: Text.HorizontalFit
                                                minimumPixelSize: theme.typeMicro
                                                elide: Text.ElideNone
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: theme.space6

                                                Label {
                                                    text: delegateExtension
                                                    color: theme.textFaint
                                                    font.family: theme.fontMono
                                                    font.pixelSize: theme.typeMicro
                                                }

                                                Label {
                                                    id: folderPathLabel
                                                    Layout.fillWidth: true
                                                    visible: root.sourceRootIsFileSelection
                                                    text: importMediaCard.delegateFolderLabel
                                                    color: theme.textMuted
                                                    font.family: theme.fontMono
                                                    font.pixelSize: theme.typeMicro
                                                    elide: Text.ElideLeft
                                                    horizontalAlignment: Text.AlignRight
                                                    HoverHandler { id: folderPathHover }
                                                    AppToolTip {
                                                        targetItem: folderPathLabel
                                                        visible: folderPathHover.hovered
                                                        text: folderPathLabel.text
                                                    }
                                                }

                                                Item { Layout.fillWidth: true }

                                                Label {
                                                    text: selected ? "Picked" : ""
                                                    visible: selected
                                                    color: theme.success
                                                    font.family: theme.fontUi
                                                    font.pixelSize: theme.typeMicro
                                                    font.weight: Font.Medium
                                                }
                                            }
                                        }

                                        MouseArea {
                                            id: mediaMouseArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: {
                                                importMediaCard.forceActiveFocus()
                                                root.toggleSelection(delegateFilePath)
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
                                        text: root.mediaQuickFilter === "all" ? "No media" : ("No " + root.mediaFilterLabel().toLowerCase())
                                        color: theme.textStrong
                                        font.family: theme.fontDisplay
                                        font.pixelSize: theme.typePageTitle
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.mediaQuickFilter === "all"
                                              ? "No supported media in this folder. Choose another folder or select files directly."
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
                                            tooltipText: root.supportedImportTypesText()
                                            onClicked: root.browseForFiles()
                                        }

                                        ActionButton {
                                            text: "Choose Folder"
                                            iconName: "folder"
                                            compact: true
                                            onClicked: root.browseForSource()
                                        }
                                    }

                                    ActionButton {
                                        visible: root.mediaQuickFilter !== "all"
                                        iconName: "media"
                                        text: "Show All"
                                        compact: true
                                        onClicked: root.mediaQuickFilter = "all"
                                    }
                                }

                                Column {
                                    objectName: "importNoSourceState"
                                    readonly property string visibilityRole: "contextual|no-source-pick-left"
                                    visible: !root.hasSource
                                    anchors.centerIn: parent
                                    spacing: theme.space10

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 46
                                        height: 46
                                        radius: theme.radius16
                                        color: theme.alpha(theme.ink800, 0.9)
                                        border.width: theme.hairlineWidth
                                        border.color: theme.alpha(theme.lineSoft, 0.2)

                                        AppIcon {
                                            anchors.centerIn: parent
                                            name: "source"
                                            color: theme.textStrong
                                            implicitWidth: 18
                                            implicitHeight: 18
                                        }
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "No source"
                                        color: theme.textStrong
                                        font.family: theme.fontDisplay
                                        font.pixelSize: theme.typePageTitle
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "Pick one on the left."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeBody
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
            visible: root.importProjectChooserOpen
            Layout.preferredWidth: root.width < theme.scaled(1120) ? 0 : (visible ? theme.scaled(390) : 0)
            Layout.minimumWidth: root.width < theme.scaled(1120) ? 0 : theme.scaled(340)
            Layout.fillWidth: root.width < theme.scaled(1120) && visible
            Layout.fillHeight: true
            title: "Destination"
            subtitle: ""
            accent: theme.accentWarm400
            headerVisible: false

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.importProjectChooserOpen
                radius: theme.radius12
                color: theme.alpha(theme.ink950, 0.22)
                border.width: theme.hairlineWidth
                border.color: theme.alpha(theme.lineSoft, 0.18)
                clip: true

                ScrollView {
                    id: importPreflightScroll
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        id: projectPickerColumn
                        x: theme.space12
                        width: Math.max(1, importPreflightScroll.availableWidth - (theme.space12 * 2))
                        spacing: theme.space10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.space2

                        Label {
                            text: "Import Preflight"
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeBody
                            font.weight: Font.DemiBold
                        }

                        Item { Layout.fillWidth: true }

                        ActionButton {
                            text: "Back"
                            iconName: "chevron-left"
                            compact: true
                            onClicked: root.importProjectChooserOpen = false
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.pendingImportKind === "folder"
                              ? "Review this folder before anything changes."
                              : "Review the selected files before anything changes."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: theme.space6

                        Label {
                            text: "Workflow"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space6

                            ActionButton {
                                objectName: "importPersonalWorkflowButton"
                                Layout.fillWidth: true
                                text: "Personal"
                                iconName: "media"
                                active: root.workflowProfile === "personal"
                                onClicked: root.workflowProfile = "personal"
                            }

                            ActionButton {
                                objectName: "importStudioWorkflowButton"
                                Layout.fillWidth: true
                                text: "Studio"
                                iconName: "settings"
                                active: root.workflowProfile === "studio"
                                onClicked: root.workflowProfile = "studio"
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.workflowProfile === "personal"
                                  ? "Defaults: managed copy, date + camera folders, keep RAW + JPEG, skip duplicates. Optional originals backup is verified after copying."
                                  : "Control file handling, duplicate behavior, destination, and project assignment."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            wrapMode: Text.WordWrap
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.workflowProfile === "studio"
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
                        Layout.fillWidth: true
                        visible: root.workflowProfile === "studio"
                        spacing: theme.space6

                        Label {
                            text: "Import mode"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space6

                            ActionButton {
                                objectName: "importManagedModeButton"
                                Layout.fillWidth: true
                                text: "Managed Copy"
                                iconName: "save"
                                active: root.importMode === "managed"
                                onClicked: root.importMode = "managed"
                            }

                            ActionButton {
                                objectName: "importReferencedModeButton"
                                Layout.fillWidth: true
                                text: "Leave in Place"
                                iconName: "source"
                                active: root.importMode === "referenced"
                                onClicked: root.importMode = "referenced"
                            }
                        }

                        Label {
                            objectName: "importModeOutcomeLabel"
                            Layout.fillWidth: true
                            text: catalogAssets.importPreflightBusy
                                  ? "Checking files… " + catalogAssets.importPreflightScannedCount + " scanned"
                                  : root.importPreflight.outcomeText || "Choose how Looksy should keep these originals."
                            color: theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.workflowProfile === "studio" && root.importMode === "managed"
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
                        Layout.fillWidth: true
                        visible: root.workflowProfile === "studio"
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

                    Rectangle {
                        objectName: "importPreflightFacts"
                        Layout.fillWidth: true
                        implicitHeight: preflightFactsColumn.implicitHeight + (theme.space10 * 2)
                        radius: theme.radius10
                        color: theme.alpha(theme.ink900, 0.78)
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.18)

                        ColumnLayout {
                            id: preflightFactsColumn
                            anchors.fill: parent
                            anchors.margins: theme.space10
                            spacing: theme.space4

                            Label {
                                Layout.fillWidth: true
                                text: "Source · " + (root.importPreflight.sourcePath || root.pendingImportRootPath)
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAnywhere
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.importPreflight.backupEnabled
                                      ? "Backup · " + (root.importPreflight.backupDestinationPath || "Checking…")
                                      : "Backup · Not configured"
                                color: root.importPreflight.backupEnabled ? theme.success : theme.warning
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAnywhere
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.importPreflight.backupEnabled
                                      ? "Primary " + (root.importPreflight.primaryEstimatedDiskText || "0 B") +
                                        " · Backup " + (root.importPreflight.backupEstimatedDiskText || "0 B") +
                                        " · Verified with " + (root.importPreflight.copyVerification || "SHA-256")
                                      : "Primary " + (root.importPreflight.primaryEstimatedDiskText || "0 B") +
                                        " · Add an originals backup in Preferences for a second verified copy."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WordWrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Destination · " + (root.importPreflight.destinationPath || "Checking…")
                                color: theme.textMain
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAnywhere
                            }

                            Label {
                                Layout.fillWidth: true
                                text: (root.importPreflight.importableCount || 0) + " ready · " +
                                      (root.importPreflight.photoCount || 0) + " photos · " +
                                      (root.importPreflight.videoCount || 0) + " videos · " +
                                      (root.importPreflight.audioCount || 0) + " audio"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Selected " + (root.importPreflight.selectedSizeText || "0 B") +
                                      " · Additional disk " + (root.importPreflight.estimatedDiskText || "0 B")
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "RAW + JPEG pairs · " + (root.importPreflight.rawJpegPairCount || 0)
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.importPreflight.organizationText || "Preserve source folders and filenames; no renaming."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeMicro
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: (root.importPreflight.duplicateCount || 0) > 0
                                 && root.workflowProfile === "studio"
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

                    Repeater {
                        model: root.importPreflight.issues || []

                        delegate: Rectangle {
                            required property var modelData

                            Layout.fillWidth: true
                            implicitHeight: importIssueColumn.implicitHeight + (theme.space8 * 2)
                            radius: theme.radius8
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


                    Rectangle {
                        Layout.fillWidth: true
                        visible: root.projectTarget.length > 0
                        implicitHeight: currentProjectRow.implicitHeight + (theme.space8 * 2)
                        radius: theme.radius10
                        color: theme.alpha(theme.accentWarm400, 0.1)
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.accentWarm400, 0.22)

                        RowLayout {
                            id: currentProjectRow
                            anchors.fill: parent
                            anchors.margins: theme.space8
                            spacing: theme.space8

                            AppIcon {
                                name: "grid"
                                color: theme.accentWarm300
                                implicitWidth: 14
                                implicitHeight: 14
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.projectTarget
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                font.weight: Font.Medium
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: theme.typeMicro
                                elide: Text.ElideNone
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: theme.space6

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Photo Library"
                            iconName: "library"
                            active: root.projectTarget.length === 0
                            onClicked: root.projectTarget = ""
                        }

                        Label {
                            visible: catalogAssets.projectNames.length > 0
                            text: "Projects"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.Medium
                        }

                        Repeater {
                            model: catalogAssets.projectNames

                            delegate: ActionButton {
                                required property string modelData

                                Layout.fillWidth: true
                                text: modelData
                                iconName: "grid"
                                active: root.projectTarget === modelData
                                onClicked: root.projectTarget = modelData
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        visible: catalogAssets.projectNames.length === 0
                        implicitHeight: 44
                        radius: theme.radius10
                        color: theme.alpha(theme.ink900, 0.76)
                        border.width: theme.hairlineWidth
                        border.color: theme.alpha(theme.lineSoft, 0.14)

                        Label {
                            anchors.centerIn: parent
                            text: "No projects yet"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                    }

                    ToneTextField {
                        id: newProjectField
                        Layout.fillWidth: true
                        placeholderText: "Create new project"
                        onAccepted: {
                            root.createProjectAndSelect(text)
                            text = ""
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.space8

                        ActionButton {
                            Layout.fillWidth: true
                            text: "New Project"
                            iconName: "plus"
                            enabled: newProjectField.text.trim().length > 0
                            onClicked: {
                                root.createProjectAndSelect(newProjectField.text)
                                newProjectField.text = ""
                            }
                        }

                        ActionButton {
                            text: ""
                            iconName: "delete"
                            showText: false
                            compact: true
                            tooltipText: "Clear project"
                            enabled: root.projectTarget.length > 0
                            onClicked: root.projectTarget = ""
                        }
                    }

                    }
                }
            }

            RowLayout {
                objectName: "importPreflightPrimaryActions"
                Layout.fillWidth: true
                spacing: theme.space8

                ActionButton {
                    Layout.fillWidth: true
                    text: "Cancel"
                    onClicked: root.importProjectChooserOpen = false
                }

                ActionButton {
                    objectName: "importPreflightConfirmButton"
                    Layout.fillWidth: true
                    text: catalogAssets.importPreflightBusy ? "Checking files…" : root.importPreflight.importableCount
                          ? "Import " + root.importPreflight.importableCount +
                            (root.importPreflight.backupEnabled ? " with Backup" : "")
                          : "Import"
                    iconName: "save"
                    tone: "primary"
                    enabled: root.canConfirmImport
                    onClicked: root.confirmSelectedImport()
                }
            }
        }
    }
}
}
