import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
    property var selectedPathsByFile: ({})
    property int selectionRevision: 0

    readonly property bool hasSource: sourceRootPath.length > 0
    readonly property bool canGoUp: currentFolderPath.length > 0 && currentFolderPath !== sourceRootPath
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
        for (let index = 0; index < fileModel.count; ++index)
            nextSelection[fileModel.get(index, "filePath")] = true
        selectedPathsByFile = nextSelection
        selectionRevision += 1
    }

    function syncProjectTarget() {
        if (projectTarget.length > 0 && catalogAssets.projectNames.indexOf(projectTarget) !== -1)
            return
        projectTarget = ""
    }

    function openSource(path, url, label) {
        sourceRootPath = path
        sourceRootUrl = url
        sourceLabel = label
        currentFolderPath = path
        currentFolderUrl = url
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
        catalogAssets.importSelection(sourceRootPath, selectedFilePaths, projectTarget)
    }

    function importCurrentFolder() {
        if (currentFolderPath.length === 0)
            return
        catalogAssets.importSelection(currentFolderPath, [], projectTarget)
    }

    Component.onCompleted: {
        catalogAssets.refreshImportSources()
        syncProjectTarget()
        ensureDefaultSource()
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

    RowLayout {
        anchors.fill: parent
        spacing: theme.space8

        BrandPanel {
            Layout.preferredWidth: 236
            Layout.fillHeight: true
            title: "Sources"
            subtitle: "Mounted volumes, camera cards, and working folders staged for intake."
            accent: theme.accentCool400

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: theme.space8

                    Repeater {
                        model: catalogAssets.importSources

                        delegate: Rectangle {
                            required property var modelData

                            Layout.fillWidth: true
                            implicitHeight: 64
                            radius: theme.radius10
                            color: root.sourceRootPath === modelData.path
                                   ? theme.alpha(theme.accentCool500, 0.12)
                                   : theme.alpha(theme.ink900, 0.88)
                            border.width: 1
                            border.color: root.sourceRootPath === modelData.path
                                          ? theme.alpha(theme.accentWarm400, 0.82)
                                          : theme.alpha(theme.lineSoft, 0.92)

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: theme.sectionPadding
                                spacing: 2

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.subtitle
                                    color: root.sourceRootPath === modelData.path ? theme.textMain : theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: root.openSource(modelData.path, modelData.url, modelData.label)
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
            border.color: theme.alpha(theme.lineSoft, 0.34)

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
                    Layout.preferredHeight: 144
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink900, 0.92)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.9)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: theme.space16
                        spacing: theme.space10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space14

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3

                                Label {
                                    text: "Import"
                                    color: theme.textStrong
                                    font.family: theme.fontDisplay
                                    font.pixelSize: 30
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    text: root.hasSource
                                          ? root.sourceLabel + "  •  " + fileModel.count + " visible  •  " + root.selectedFileCount + " selected"
                                          : "Choose a source to begin staging media."
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: root.currentFolderPath.length > 0 ? root.currentFolderPath : "Select a source from the left rail."
                                    color: theme.accentWarm300
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            RowLayout {
                                spacing: theme.gapControl

                                ActionButton {
                                    text: "Up One Folder"
                                    enabled: root.canGoUp
                                    onClicked: root.goUp()
                                }

                                ActionButton {
                                    text: "Select Visible"
                                    enabled: fileModel.count > 0
                                    onClicked: root.selectAllVisible()
                                }

                                ActionButton {
                                    text: "Clear"
                                    enabled: root.selectedFileCount > 0
                                    onClicked: root.clearSelection()
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            Rectangle {
                                radius: theme.radiusChip
                                color: theme.alpha(theme.accentWarm500, 0.14)
                                border.width: 1
                                border.color: theme.alpha(theme.accentWarm400, 0.3)
                                implicitWidth: statusLabel.implicitWidth + theme.space12 * 2
                                implicitHeight: 24

                                Label {
                                    id: statusLabel
                                    anchors.centerIn: parent
                                    text: catalogAssets.status
                                    color: theme.textMain
                                    font.family: theme.fontMono
                                    font.pixelSize: 9
                                    font.weight: Font.Medium
                                }
                            }

                            Item { Layout.fillWidth: true }

                            Label {
                                text: catalogAssets.driveCount + " drives"
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 9
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: theme.space10

                    BrandPanel {
                        Layout.preferredWidth: 240
                        Layout.fillHeight: true
                        title: "Folders"
                        subtitle: root.hasSource ? "Move within the active source without leaving intake." : "Choose a source to start browsing."
                        accent: theme.accentWarm400

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: theme.space8
                            model: directoryModel

                            delegate: Rectangle {
                                required property int index
                                required property string fileName
                                required property string fileUrl

                                width: ListView.view.width
                                height: 46
                                radius: theme.radius8
                                color: theme.alpha(theme.ink900, 0.84)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.9)

                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: theme.space12
                                    anchors.right: parent.right
                                    anchors.rightMargin: theme.space12
                                    text: fileName
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: root.openFolder(fileUrl)
                                }
                            }
                        }

                        Label {
                            visible: root.currentFolderPath.length > 0 && directoryModel.count === 0
                            Layout.fillWidth: true
                            text: "No subfolders in this location."
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: 11
                        }
                    }

                    BrandPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        title: "Visible Media"
                        subtitle: root.hasSource
                                  ? "Select one or many files before sending them into the catalog."
                                  : "Supported photo and video files will appear here."
                        accent: theme.accentCool400

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            GridView {
                                anchors.fill: parent
                                clip: true
                                cellWidth: 192
                                cellHeight: 228
                                boundsBehavior: Flickable.StopAtBounds
                                model: fileModel

                                delegate: Rectangle {
                                    required property int index
                                    required property string filePath
                                    required property string fileName
                                    required property string fileUrl

                                    readonly property string delegateFilePath: filePath
                                    readonly property string delegateFileName: fileName
                                    readonly property string delegateFileUrl: fileUrl
                                    readonly property bool delegateIsVideo: root.isVideoFile(filePath)
                                    readonly property bool selected: !!root.selectedPathsByFile[filePath]
                                    readonly property string delegateExtension: {
                                        const parts = delegateFileName.split(".")
                                        return parts.length > 1 ? parts[parts.length - 1].toUpperCase() : "FILE"
                                    }

                                    width: 180
                                    height: 214
                                    radius: theme.radius12
                                    color: selected
                                           ? theme.alpha(theme.accentCool500, 0.09)
                                           : theme.alpha(theme.ink900, 0.86)
                                    border.width: 1
                                    border.color: selected
                                                  ? theme.alpha(theme.accentWarm400, 0.82)
                                                  : theme.alpha(theme.lineSoft, 0.86)

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: theme.space8
                                        spacing: theme.space8

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 154
                                            radius: theme.radius10
                                            color: theme.ink950
                                            clip: true

                                            Image {
                                                id: previewImage
                                                anchors.fill: parent
                                                source: delegateIsVideo ? "" : delegateFileUrl
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                                cache: false
                                                visible: !delegateIsVideo && status === Image.Ready
                                            }

                                            Rectangle {
                                                anchors.fill: parent
                                                visible: delegateIsVideo || previewImage.status !== Image.Ready
                                                color: theme.alpha(theme.ink900, 0.88)

                                                Column {
                                                    anchors.centerIn: parent
                                                    spacing: theme.space6

                                                    Label {
                                                        anchors.horizontalCenter: parent.horizontalCenter
                                                        text: delegateIsVideo ? "VIDEO" : delegateExtension
                                                        color: theme.accentWarm300
                                                        font.family: theme.fontMono
                                                        font.pixelSize: 15
                                                        font.weight: Font.DemiBold
                                                    }

                                                    Label {
                                                        anchors.horizontalCenter: parent.horizontalCenter
                                                        text: delegateIsVideo ? delegateExtension + " clip" : "Preview unavailable"
                                                        color: theme.textMuted
                                                        font.family: theme.fontUi
                                                        font.pixelSize: 10
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                anchors.top: parent.top
                                                anchors.right: parent.right
                                                anchors.topMargin: theme.space8
                                                anchors.rightMargin: theme.space8
                                                width: 24
                                                height: 24
                                                radius: 12
                                                color: selected ? theme.accentWarm500 : theme.alpha(theme.ink900, 0.82)
                                                border.width: 1
                                                border.color: selected
                                                              ? theme.alpha(theme.accentWarm300, 0.72)
                                                              : theme.alpha(theme.lineStrong, 0.9)

                                                Label {
                                                    anchors.centerIn: parent
                                                    text: selected ? "✓" : "+"
                                                    color: selected ? theme.ink950 : theme.textMain
                                                    font.family: theme.fontUi
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                }
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: delegateFileName
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: root.parentPathForFile(delegateFilePath)
                                            color: theme.textFaint
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                            elide: Text.ElideMiddle
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: root.toggleSelection(delegateFilePath)
                                    }
                                }
                            }

                            Column {
                                anchors.centerIn: parent
                                visible: root.currentFolderPath.length > 0 && fileModel.count === 0
                                spacing: theme.space8

                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "No supported media here."
                                    color: theme.textStrong
                                    font.family: theme.fontDisplay
                                    font.pixelSize: 22
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Open another folder or choose a different source."
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                }
                            }

                            Column {
                                anchors.centerIn: parent
                                visible: !root.hasSource
                                spacing: theme.space8

                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "No source selected"
                                    color: theme.textStrong
                                    font.family: theme.fontDisplay
                                    font.pixelSize: 22
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Pick a source from the left rail to begin intake."
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

        BrandPanel {
            Layout.preferredWidth: 322
            Layout.fillHeight: true
            title: "Import Queue"
            subtitle: root.selectedFileCount > 0
                      ? root.selectedFileCount + " files staged from " + (root.sourceLabel.length > 0 ? root.sourceLabel : "the current source")
                      : "Select files for a targeted import, or ingest the current folder recursively."
            accent: theme.accentWarm400

            Label {
                Layout.fillWidth: true
                text: "Project"
                color: theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }

            ToneTextField {
                id: newProjectField
                Layout.fillWidth: true
                placeholderText: "Create a project"
                onAccepted: {
                    catalogAssets.createProject(text)
                    text = ""
                }
            }

            ToneComboBox {
                Layout.fillWidth: true
                model: root.projectOptionLabels
                currentIndex: Math.max(0, root.projectOptionValues.indexOf(root.projectTarget))
                onActivated: root.projectTarget = root.projectOptionValues[currentIndex]
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8

                ActionButton {
                    Layout.fillWidth: true
                    text: "Create"
                    enabled: newProjectField.text.trim().length > 0
                    onClicked: {
                        catalogAssets.createProject(newProjectField.text)
                        newProjectField.text = ""
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: theme.controlHeight
                    radius: theme.radiusPanel
                    color: theme.alpha(theme.ink900, 0.92)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.92)

                    Label {
                        anchors.centerIn: parent
                        text: root.projectTarget.length > 0 ? root.projectTarget : "No Project"
                        color: root.projectTarget.length > 0 ? theme.textMain : theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: catalogAssets.importing ? "Importing..." : "Import Selection"
                tone: "primary"
                enabled: root.selectedFileCount > 0 && !catalogAssets.busy
                onClicked: root.importSelected()
            }

            ActionButton {
                Layout.fillWidth: true
                text: catalogAssets.importing ? "Importing..." : "Import Folder"
                enabled: root.currentFolderPath.length > 0 && !catalogAssets.busy
                onClicked: root.importCurrentFolder()
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Clear Selection"
                enabled: root.selectedFileCount > 0
                onClicked: root.clearSelection()
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: theme.radius12
                color: theme.alpha(theme.ink900, 0.9)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.9)

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: theme.space10
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: theme.space8

                        Repeater {
                            model: root.selectedFilePaths

                            delegate: Rectangle {
                                required property string modelData

                                Layout.fillWidth: true
                                implicitHeight: 56
                                radius: theme.radius8
                                color: theme.alpha(theme.ink850, 0.92)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.88)

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: theme.space10
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: root.fileNameForPath(modelData)
                                        color: theme.textStrong
                                        font.family: theme.fontUi
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: root.parentPathForFile(modelData)
                                        color: theme.textFaint
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        elide: Text.ElideMiddle
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: root.toggleSelection(modelData)
                                }
                            }
                        }

                        Label {
                            visible: root.selectedFileCount === 0
                            Layout.fillWidth: true
                            text: "Selected photos and videos will appear here. Project assignment stays optional."
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
