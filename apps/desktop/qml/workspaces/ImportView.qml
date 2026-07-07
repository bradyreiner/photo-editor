import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.folderlistmodel
import "../components"

Rectangle {
    id: root
    objectName: "importWorkspace"
    color: "transparent"

    Theme { id: theme }

    readonly property string importVisibilityContract: "import-visibility:v1|always:sources-folders-header-media-grid-primary-import|contextual:destination-panel-selection-actions-filter-empty-progress|one-expansion:import-options-project-target-refresh-sources-clear-selection|hover-focus:media-selection-source-folder-keyboard|diagnostic:import-progress-source-empty|preserve:functionality"
    readonly property string importSurfaceContract: "import-surfaces:v1|sources:left-rail|folders:navigation-list|media:grid-filter-selection|destination:contextual-project-picker|actions:split-import-menu|no-elide:true|no-overlap:true"
    readonly property string importWorkflowPolishContract: "import-workflow-polish:v1|first-glance:sources-current-folder-media-grid-primary-import|close-inspection:source-confirmation-filter-counts-selection-destination-progress|clutter:single-progress-place-split-import-options|interaction:choose-source-select-visible-clear-import-retry-refresh|text:no-elide-no-overlap-paths-wrap|functionality:preserved"
    readonly property string importPageCompletionContract: "page-completion:v1|import|sidebar:sources-folders-refresh|content:header-filter-media-grid-empty|destination:project-picker-create-confirm-cancel|toolbar:select-visible-clear-primary-import|states:no-source-empty-filtered-selected-importing-failed-complete"

    property string sourceRootPath: ""
    property string sourceRootUrl: ""
    property string sourceLabel: ""
    property string pendingFolderPath: ""
    property string pendingFolderUrl: ""
    property string currentFolderPath: ""
    property string currentFolderUrl: ""
    property string projectTarget: ""
    property bool importProjectChooserOpen: false
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
    readonly property bool canImportFolder: currentFolderPath.length > 0 && !catalogAssets.busy
    readonly property string importActionContract: "import-actions:v1|split-button:true|primary:import-to-virel-vault|menu:import-selection,import-folder,import-with-preset,select-visible,clear-selection,refresh-sources|disabled-reasons:true|no-clutter:true"
    readonly property string currentFolderName: currentFolderPath.length > 0 ? fileNameForPath(currentFolderPath) : "No folder"
    readonly property int visiblePhotoCount: {
        const total = fileModel.count
        let count = 0
        for (let index = 0; index < total; ++index) {
            if (!isVideoFile(fileModel.get(index, "filePath")))
                count += 1
        }
        return count
    }
    readonly property int visibleVideoCount: {
        const total = fileModel.count
        let count = 0
        for (let index = 0; index < total; ++index) {
            if (isVideoFile(fileModel.get(index, "filePath")))
                count += 1
        }
        return count
    }
    readonly property var filteredFileEntries: {
        selectionRevision
        const filterMode = mediaQuickFilter
        const total = fileModel.count
        let entries = []
        for (let index = 0; index < total; ++index) {
            const filePath = fileModel.get(index, "filePath")
            const isVideo = root.isVideoFile(filePath)
            if (filterMode === "photos" && isVideo)
                continue
            if (filterMode === "videos" && !isVideo)
                continue
            if (filterMode === "selected" && !selectedPathsByFile[filePath])
                continue

            const fileName = fileModel.get(index, "fileName")
            entries.push({
                           filePath: filePath,
                           fileName: fileName,
                           fileUrl: fileModel.get(index, "fileUrl"),
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
        if (lastSlash <= 0)
            return ""
        return filePath.slice(0, lastSlash)
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
        return "Supported: RAW, JPG, PNG, TIFF, HEIC, MP4, MOV, M4V, MPEG, WEBM."
    }

    function sourceConfirmationText() {
        if (!root.hasSource)
            return "Choose Source"
        if (root.currentFolderPath.length === 0)
            return "Source Ready"
        return "Folder Ready"
    }

    function selectionMixText() {
        if (selectedFileCount === 0)
            return String(filteredFileCount) + " visible"
        if (selectedPhotoCount > 0 && selectedVideoCount > 0)
            return selectedPhotoCount + " photo / " + selectedVideoCount + " video"
        if (selectedVideoCount > 0)
            return selectedVideoCount + (selectedVideoCount === 1 ? " video" : " videos")
        return selectedPhotoCount + (selectedPhotoCount === 1 ? " photo" : " photos")
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
        currentFolderPath = ""
        currentFolderUrl = ""
        pendingFolderPath = path
        pendingFolderUrl = url
        deferredFolderLoadTimer.restart()
        importProjectChooserOpen = false
        clearSelection()
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
        importProjectChooserOpen = true
    }

    function confirmSelectedImport() {
        if (!canImportSelection)
            return
        importProjectChooserOpen = false
        catalogAssets.importSelection(sourceRootPath, selectedFilePaths, projectTarget)
    }

    function importCurrentFolder() {
        if (currentFolderPath.length === 0)
            return
        catalogAssets.importSelection(currentFolderPath, [], projectTarget)
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
                disabledReason: root.currentFolderPath.length > 0 ? "No importable media found in this folder." : "Choose a folder first.",
                tooltip: "Import the current folder."
            },
            {
                key: "import-with-preset",
                label: "Import With Preset",
                icon: "swatch",
                enabled: false,
                disabledReason: "Import preset application needs controller support before it can run.",
                tooltip: "Apply metadata or develop defaults during import."
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
            return
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
        if (!visible)
            return
        catalogAssets.refreshImportSources()
        ensureDefaultSource()
    }

    onSelectedFileCountChanged: {
        if (selectedFileCount === 0)
            importProjectChooserOpen = false
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

            for (let index = 0; index < catalogAssets.importSources.length; ++index) {
                const source = catalogAssets.importSources[index]
                if (source.path === root.sourceRootPath)
                    return
            }

            root.sourceRootPath = ""
            root.sourceRootUrl = ""
            root.sourceLabel = ""
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
        spacing: theme.space8

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: theme.space8

            BrandPanel {
                objectName: "importSourcePanel"
                readonly property string visibilityRole: "always-visible|source-list-folder-nav-folder-list-refresh"
                Layout.preferredWidth: 216
                Layout.fillHeight: true
                title: "Sources"
                subtitle: ""
                accent: theme.accentCool400

            ListView {
                id: sourceListView
                objectName: "importSourceList"
                readonly property string visibilityRole: "always-visible|mounted-sources-select"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 320)
                Layout.fillHeight: false
                clip: true
                spacing: theme.space8
                model: catalogAssets.importSources
                boundsBehavior: Flickable.StopAtBounds

                delegate: ImportSourceRow {
                    required property string label
                    required property string subtitle
                    required property string path
                    required property string url

                    sourceUrl: url
                    selected: root.sourceRootPath === path
                    onActivated: function(path, sourceUrl, label) {
                        root.openSource(path, sourceUrl, label)
                    }
                }
            }

            Label {
                visible: catalogAssets.importSources.length === 0
                Layout.fillWidth: true
                text: "No import sources are available yet. Refresh sources to rescan mounted storage."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Label {
                visible: root.hasSource
                Layout.fillWidth: true
                text: "Folders"
                color: theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: 9
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
                border.width: 1
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
                        spacing: 1

                        Label {
                            Layout.fillWidth: true
                            text: root.currentFolderName
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 8
                            elide: Text.ElideNone
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.sourceLabel
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: 9
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 7
                            elide: Text.ElideNone
                        }
                    }

                    Rectangle {
                        implicitWidth: folderCountLabel.implicitWidth + (theme.space8 * 2)
                        implicitHeight: folderCountLabel.implicitHeight + theme.space6
                        radius: 999
                        color: theme.alpha(theme.ink800, 0.92)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.16)

                        Label {
                            id: folderCountLabel
                            anchors.centerIn: parent
                            text: String(directoryModel.count)
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 9
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
                spacing: theme.space8
                model: directoryModel
                boundsBehavior: Flickable.StopAtBounds

                delegate: ImportFolderRow {
                    required property string fileName
                    required property string fileUrl

                    selected: root.currentFolderUrl === fileUrl
                    onActivated: function(fileUrl) { root.openFolder(fileUrl) }
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Refresh Sources"
                iconName: "source"
                enabled: !catalogAssets.busy
                onClicked: catalogAssets.refreshImportSources(true)
            }
        }

        Rectangle {
            objectName: "importMainPanel"
            readonly property string surfaceContract: "import-main-panel:v1|header:always|media-grid:always|filter:always|selection-actions:always|empty-states:contextual|primary-import:always|no-elide:true|no-overlap:true"
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radiusSurface
            color: theme.ink850
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.18)

            gradient: Gradient {
                GradientStop { position: 0.0; color: theme.alpha(theme.accentCool500, 0.05) }
                GradientStop { position: 0.14; color: theme.alpha(theme.ink850, 0.98) }
                GradientStop { position: 1.0; color: theme.ink900 }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.panelPadding
                spacing: theme.gapPanel

                Rectangle {
                    objectName: "importHeaderBar"
                    readonly property string visibilityRole: "always-visible|source-path-counts-select-visible-clear-import"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(104, importHeaderContent.implicitHeight + theme.space24)
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink900, 0.92)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.3)

                    RowLayout {
                        id: importHeaderContent
                        anchors.fill: parent
                        anchors.margins: theme.space12
                        spacing: theme.space12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: theme.space4

                            Label {
                                Layout.fillWidth: true
                                text: root.hasSource ? root.sourceLabel : "Pick a source"
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: 22
                                font.weight: Font.DemiBold
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: 2
                                elide: Text.ElideNone
                            }

                            Label {
                                Layout.fillWidth: true
                                text: root.currentFolderPath.length > 0 ? root.currentFolderPath : "Choose a mounted drive or folder."
                                color: root.currentFolderPath.length > 0 ? theme.accentWarm300 : theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 2
                                elide: Text.ElideNone
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space6

                                StatusChip {
                                    objectName: "importSourceConfirmationChip"
                                    text: root.sourceConfirmationText()
                                    iconName: root.hasSource ? "check" : "source"
                                    tone: root.hasSource ? "success" : "neutral"
                                    compact: true
                                    minimumReadableWidth: 108
                                    maximumReadableWidth: 168
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
                                    minimumReadableWidth: 118
                                    maximumReadableWidth: 192
                                    tooltipText: root.selectedFileCount > 0
                                                 ? "Selection includes " + root.selectedPhotoCount + " photos and " + root.selectedVideoCount + " videos."
                                                 : root.filteredFileCount + " visible files in this folder."
                                }
                            }
                        }

                        Label {
                            text: String(root.filteredFileCount) + " media"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                        }

                        Label {
                            text: String(root.selectedFileCount) + " selected"
                            color: root.hasStagedSelection ? theme.success : theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }

                        ActionButton {
                            iconName: "plus"
                            text: "Select Visible"
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
                            text: root.hasActiveImportJobs() ? "Importing" : "Import to VIREL Vault"
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
                                border.width: 1
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
                                            font.pixelSize: 10
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
                                    property int columns: Math.max(2, Math.floor(width / 250))
                                    property int gutter: theme.space8
                                    cellWidth: Math.floor((width - ((columns - 1) * gutter)) / columns)
                                    cellHeight: Math.max(176, (cellWidth - gutter) * 0.74) + 34
                                    boundsBehavior: Flickable.StopAtBounds
                                    model: root.filteredFileEntries

                                    delegate: FocusScope {
                                        id: importMediaCard
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
                                            readonly property real cardAspectRatio: 0.74
                                            height: Math.max(176, width * cardAspectRatio)
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
                                                        font.pixelSize: 14
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
                                                border.width: 1
                                                border.color: selected
                                                              ? theme.alpha(theme.accentWarm300, 0.92)
                                                              : theme.alpha(theme.lineStrong, 0.42)

                                                Label {
                                                    anchors.centerIn: parent
                                                    text: selected ? "✓" : "+"
                                                    color: selected ? theme.ink950 : theme.textMain
                                                    font.family: theme.fontUi
                                                    font.pixelSize: 12
                                                    font.weight: Font.DemiBold
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            id: infoColumn
                                            anchors.top: previewFrame.bottom
                                            anchors.topMargin: theme.space4
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            spacing: 1

                                            Label {
                                                Layout.fillWidth: true
                                                text: delegateFileName
                                                color: theme.textStrong
                                                font.family: theme.fontUi
                                                font.pixelSize: 11
                                                font.weight: Font.Medium
                                                fontSizeMode: Text.HorizontalFit
                                                minimumPixelSize: 8
                                                elide: Text.ElideNone
                                            }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: theme.space6

                                                Label {
                                                    text: delegateExtension
                                                    color: theme.textFaint
                                                    font.family: theme.fontMono
                                                    font.pixelSize: 9
                                                }

                                                Item { Layout.fillWidth: true }

                                                Label {
                                                    text: selected ? "Picked" : ""
                                                    visible: selected
                                                    color: theme.success
                                                    font.family: theme.fontUi
                                                    font.pixelSize: 9
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
                                        radius: 14
                                        color: theme.alpha(theme.ink800, 0.9)
                                        border.width: 1
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
                                        font.pixelSize: 22
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.mediaQuickFilter === "all"
                                              ? root.supportedImportTypesText()
                                              : "Try another filter or show all supported media."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 12
                                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        width: Math.min(420, root.width - 96)
                                        horizontalAlignment: Text.AlignHCenter
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
                                        radius: 14
                                        color: theme.alpha(theme.ink800, 0.9)
                                        border.width: 1
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
                                        font.pixelSize: 22
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "Pick one on the left."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 12
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
            readonly property string visibilityRole: "contextual|project-picker-create-confirm-cancel"
            visible: root.importProjectChooserOpen
            Layout.preferredWidth: visible ? 260 : 0
            Layout.fillHeight: true
            title: "Destination"
            subtitle: ""
            accent: theme.accentWarm400
            headerVisible: false

            Rectangle {
                Layout.fillWidth: true
                visible: root.importProjectChooserOpen
                implicitHeight: projectPickerColumn.implicitHeight + (theme.space12 * 2)
                radius: theme.radius12
                color: theme.alpha(theme.ink950, 0.22)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.18)

                ColumnLayout {
                    id: projectPickerColumn
                    anchors.fill: parent
                    anchors.margins: theme.space12
                    spacing: theme.space10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: "Destination"
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 12
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

                    Rectangle {
                        Layout.fillWidth: true
                        visible: root.projectTarget.length > 0
                        implicitHeight: currentProjectRow.implicitHeight + (theme.space8 * 2)
                        radius: theme.radius10
                        color: theme.alpha(theme.accentWarm400, 0.1)
                        border.width: 1
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
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 8
                                elide: Text.ElideNone
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: theme.space6

                        ActionButton {
                            Layout.fillWidth: true
                            text: "VIREL Vault"
                            iconName: "library"
                            active: root.projectTarget.length === 0
                            onClicked: root.projectTarget = ""
                        }

                        Label {
                            visible: catalogAssets.projectNames.length > 0
                            text: "Projects"
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: 10
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
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.14)

                        Label {
                            anchors.centerIn: parent
                            text: "No projects yet"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
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

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: theme.space4
                        spacing: theme.space8

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Cancel"
                            onClicked: root.importProjectChooserOpen = false
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Import"
                            iconName: "save"
                            tone: "primary"
                            enabled: root.canImportSelection
                            onClicked: root.confirmSelectedImport()
                        }
                    }
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
}
