import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.folderlistmodel
import "../components"

Rectangle {
    id: root
    color: "transparent"

    Theme { id: theme }

    property string sourceRootPath: ""
    property string sourceRootUrl: ""
    property string sourceLabel: ""
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
        currentFolderPath = path
        currentFolderUrl = url
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
        if (!canImportSelection || projectTarget.length === 0)
            return
        importProjectChooserOpen = false
        catalogAssets.importSelection(sourceRootPath, selectedFilePaths, projectTarget)
    }

    function importCurrentFolder() {
        if (currentFolderPath.length === 0)
            return
        catalogAssets.importSelection(currentFolderPath, [], projectTarget)
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

    Component.onCompleted: {
        catalogAssets.refreshImportSources()
        syncProjectTarget()
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
            root.ensureDefaultSource()
        }

        function onImportingChanged() {
            if (!catalogAssets.importing && catalogAssets.errorMessage.length === 0)
                root.clearSelection()
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
                Layout.preferredWidth: 216
                Layout.fillHeight: true
                title: "Sources"
                subtitle: ""
                accent: theme.accentCool400

            ListView {
                id: sourceListView
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 320)
                Layout.fillHeight: false
                clip: true
                spacing: theme.space8
                model: catalogAssets.importSources
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    required property string label
                    required property string subtitle
                    required property string path
                    required property string url

                    width: ListView.view.width
                    height: 64
                    radius: theme.radius10
                    color: root.sourceRootPath === path
                           ? theme.alpha(theme.accentCool500, 0.12)
                           : (sourceMouseArea.containsMouse ? theme.alpha(theme.ink800, 0.88) : theme.alpha(theme.ink900, 0.82))
                    border.width: 1
                    border.color: root.sourceRootPath === path
                                  ? theme.alpha(theme.accentWarm400, 0.64)
                                  : (sourceMouseArea.containsMouse ? theme.alpha(theme.lineSoft, 0.34) : theme.alpha(theme.lineSoft, 0.18))

                    Behavior on color {
                        ColorAnimation { duration: theme.durationFast }
                    }

                    Behavior on border.color {
                        ColorAnimation { duration: theme.durationFast }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: theme.sectionPadding
                        spacing: theme.space10

                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: 3
                            Layout.fillHeight: true
                            radius: 2
                            color: root.sourceRootPath === path
                                   ? theme.accentWarm400
                                   : theme.alpha(theme.lineSoft, sourceMouseArea.containsMouse ? 0.34 : 0.14)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: label
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: subtitle
                                color: root.sourceRootPath === path ? theme.textMain : theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            visible: root.sourceRootPath === path || sourceMouseArea.containsMouse
                            implicitWidth: sourceStateLabel.implicitWidth + (theme.space8 * 2)
                            implicitHeight: sourceStateLabel.implicitHeight + theme.space6
                            radius: 999
                            color: root.sourceRootPath === path
                                   ? theme.alpha(theme.accentWarm400, 0.2)
                                   : theme.alpha(theme.ink800, 0.92)
                            border.width: 1
                            border.color: root.sourceRootPath === path
                                          ? theme.alpha(theme.accentWarm300, 0.34)
                                          : theme.alpha(theme.lineSoft, 0.16)

                            Label {
                                id: sourceStateLabel
                                anchors.centerIn: parent
                                text: root.sourceRootPath === path ? "Active" : "Open"
                                color: root.sourceRootPath === path ? theme.accentWarm300 : theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    MouseArea {
                        id: sourceMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.openSource(path, url, label)
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
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.sourceLabel
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: 9
                            elide: Text.ElideRight
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
                visible: root.hasSource
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: theme.space8
                model: directoryModel
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    required property string fileName
                    required property string fileUrl

                    width: ListView.view.width
                    height: 40
                    radius: theme.radius8
                    color: root.currentFolderUrl === fileUrl
                           ? theme.alpha(theme.accentCool500, 0.1)
                           : (folderRailMouseArea.containsMouse ? theme.alpha(theme.ink800, 0.84) : theme.alpha(theme.ink950, 0.16))
                    border.width: 1
                    border.color: root.currentFolderUrl === fileUrl
                                  ? theme.alpha(theme.accentWarm400, 0.44)
                                  : (folderRailMouseArea.containsMouse ? theme.alpha(theme.lineSoft, 0.26) : theme.alpha(theme.lineSoft, 0.1))

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: theme.space10
                        anchors.rightMargin: theme.space10
                        spacing: theme.space8

                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: 6
                            implicitHeight: 6
                            radius: 3
                            color: root.currentFolderUrl === fileUrl
                                   ? theme.accentWarm400
                                   : theme.alpha(theme.accentCool400, folderRailMouseArea.containsMouse ? 0.82 : 0.46)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: fileName
                            color: root.currentFolderUrl === fileUrl ? theme.textStrong : theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }

                        AppIcon {
                            name: "chevron-right"
                            color: theme.textFaint
                            implicitWidth: 12
                            implicitHeight: 12
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    MouseArea {
                        id: folderRailMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.openFolder(fileUrl)
                    }
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Refresh Sources"
                iconName: "source"
                enabled: !catalogAssets.busy
                onClicked: catalogAssets.refreshImportSources()
            }
        }

        Rectangle {
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
                    Layout.fillWidth: true
                    Layout.preferredHeight: 112
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink900, 0.92)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.3)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: theme.space16
                        spacing: theme.space10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space14

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                Label {
                                    text: "Import"
                                    color: theme.textStrong
                                    font.family: theme.fontDisplay
                                    font.pixelSize: 30
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    text: root.hasSource
                                          ? root.sourceLabel + "  •  " + root.filteredFileCount
                                          : "Pick a source"
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: pathRow.implicitHeight + (theme.space8 * 2)
                                    radius: theme.radius8
                                    color: theme.alpha(theme.ink950, 0.18)
                                    border.width: 1
                                    border.color: theme.alpha(theme.lineSoft, 0.14)

                                    RowLayout {
                                        id: pathRow
                                        anchors.fill: parent
                                        anchors.margins: theme.space8
                                        spacing: theme.space8

                                        Label {
                                            id: folderPathLabel
                                            Layout.fillWidth: true
                                            text: root.currentFolderPath.length > 0 ? root.currentFolderPath : "Select a source from the left rail."
                                            color: theme.accentWarm300
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                            elide: Text.ElideMiddle
                                        }

                                        Rectangle {
                                            implicitWidth: filterSummaryLabel.implicitWidth + (theme.space8 * 2)
                                            implicitHeight: filterSummaryLabel.implicitHeight + theme.space6
                                            radius: 999
                                            color: theme.alpha(theme.ink800, 0.92)
                                            border.width: 1
                                            border.color: theme.alpha(theme.lineSoft, 0.18)

                                            Label {
                                                id: filterSummaryLabel
                                                anchors.centerIn: parent
                                                text: root.mediaFilterLabel()
                                                color: theme.textMuted
                                                font.family: theme.fontUi
                                                font.pixelSize: 9
                                                font.weight: Font.Medium
                                            }
                                        }

                                        Label {
                                            text: String(root.filteredFileCount)
                                            color: theme.textFaint
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: importUtilityRow.implicitWidth + (theme.space8 * 2)
                                implicitHeight: importUtilityRow.implicitHeight + (theme.space8 * 2)
                                radius: theme.radius12
                                color: theme.alpha(theme.ink950, 0.24)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.22)

                                RowLayout {
                                    id: importUtilityRow
                                    anchors.fill: parent
                                    anchors.margins: theme.space8
                                    spacing: theme.gapControl

                                    ActionButton {
                                        iconName: "plus"
                                        showText: false
                                        tooltipText: "Select visible"
                                        compact: true
                                        embedded: true
                                        enabled: root.filteredFileCount > 0
                                        onClicked: root.selectAllVisible()
                                    }

                                    ActionButton {
                                        iconName: "delete"
                                        showText: false
                                        tooltipText: "Clear selection"
                                        compact: true
                                        embedded: true
                                        enabled: root.selectedFileCount > 0
                                        onClicked: root.clearSelection()
                                    }
                                }
                            }
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
                                            text: "Photo"
                                            accent: theme.accentCool400
                                            active: root.mediaQuickFilter === "photos"
                                            onClicked: root.mediaQuickFilter = "photos"
                                        }

                                        FilterChip {
                                            text: "Video"
                                            accent: theme.warning
                                            active: root.mediaQuickFilter === "videos"
                                            onClicked: root.mediaQuickFilter = "videos"
                                        }

                                        FilterChip {
                                            text: "Sel"
                                            accent: theme.success
                                            active: root.mediaQuickFilter === "selected"
                                            onClicked: root.mediaQuickFilter = "selected"
                                        }

                                        Item { Layout.fillWidth: true }

                                        Label {
                                            text: root.filteredFileCount + " / " + root.selectedFileCount
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
                                                cache: false
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
                                                elide: Text.ElideRight
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
                                        text: root.mediaQuickFilter === "all" ? "Try another folder." : "Try another filter."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 12
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
            Layout.preferredWidth: 232
            Layout.fillHeight: true
            title: "Import"
            subtitle: ""
            accent: theme.accentWarm400
            headerVisible: false

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: importSummaryColumn.implicitHeight + (theme.space12 * 2)
                radius: theme.radius12
                color: theme.alpha(theme.ink950, 0.22)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.18)

                ColumnLayout {
                    id: importSummaryColumn
                    anchors.fill: parent
                    anchors.margins: theme.space12
                    spacing: theme.space10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.space10

                        ColumnLayout {
                            spacing: 2

                            Label {
                                text: "Selected"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.Medium
                            }

                            Label {
                                text: root.selectedFileCount > 0 ? (String(root.selectedFileCount) + " files") : "Nothing picked"
                                color: theme.textStrong
                                font.family: theme.fontDisplay
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            implicitWidth: 42
                            implicitHeight: 42
                            radius: 12
                            color: root.hasStagedSelection ? theme.alpha(theme.success, 0.16) : theme.alpha(theme.ink800, 0.92)
                            border.width: 1
                            border.color: root.hasStagedSelection ? theme.alpha(theme.success, 0.28) : theme.alpha(theme.lineSoft, 0.16)

                            Label {
                                anchors.centerIn: parent
                                text: String(root.selectedFileCount)
                                color: root.hasStagedSelection ? theme.success : theme.textMain
                                font.family: theme.fontDisplay
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.currentFolderName
                        color: theme.textFaint
                        font.family: theme.fontMono
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        text: root.hasActiveImportJobs() ? "Importing..." : "Import Selected"
                        iconName: "media"
                        tone: "primary"
                        enabled: root.canImportSelection
                        onClicked: root.importSelected()
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: !root.importProjectChooserOpen
                        text: root.selectedFileCount > 0 ? "Choose a destination after you click import." : "Pick files from the grid."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }

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
                            text: "Add to"
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
                                elide: Text.ElideRight
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: theme.space6

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
                            enabled: root.canImportSelection && root.projectTarget.length > 0
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
