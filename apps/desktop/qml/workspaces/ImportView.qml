import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel
import "../components"

Rectangle {
    id: root
    color: "transparent"

    property string sourceRootPath: ""
    property string sourceRootUrl: ""
    property string sourceLabel: ""
    property string currentFolderPath: ""
    property string currentFolderUrl: ""
    property string projectTarget: ""
    property var selectedPathsByFile: ({})
    property int selectionRevision: 0

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
        if (currentFolderUrl.length === 0 || currentFolderPath === sourceRootPath)
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
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            radius: 18
            color: "#14191d"
            border.width: 1
            border.color: "#242c34"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Label {
                    text: "Sources"
                    color: "#f2f4f6"
                    font.family: "Avenir Next"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                Label {
                    text: "Browse folders, mounted drives, SD cards, and removable media without leaving the app."
                    color: "#98a3ad"
                    font.family: "Avenir Next"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: sourceColumn.height
                    clip: true

                    Column {
                        id: sourceColumn
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: catalogAssets.importSources

                            delegate: Rectangle {
                                required property var modelData

                                width: sourceColumn.width
                                height: 58
                                radius: 12
                                color: root.sourceRootPath === modelData.path ? "#1b232a" : "#10151a"
                                border.width: 1
                                border.color: root.sourceRootPath === modelData.path ? "#c99a52" : "#263039"

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 2

                                    Label {
                                        text: modelData.label
                                        color: "#f2f5f7"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }

                                    Label {
                                        text: modelData.subtitle
                                        color: "#8b96a0"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.openSource(modelData.path, modelData.url, modelData.label)
                                }
                            }
                        }
                    }
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: "Refresh Sources"
                    enabled: !catalogAssets.busy
                    onClicked: catalogAssets.refreshImportSources()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 18
            color: "#14191d"
            border.width: 1
            border.color: "#242c34"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    radius: 16
                    color: "#11161b"
                    border.width: 1
                    border.color: "#273038"

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
                                    text: "Import"
                                    color: "#f3f5f7"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 28
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    text: root.sourceLabel.length > 0
                                          ? root.sourceLabel + "  •  " + fileModel.count + " visible files  •  " + root.selectedFileCount + " selected"
                                          : "Choose a source to begin"
                                    color: "#c1cad2"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 12
                                }

                                Label {
                                    text: root.currentFolderPath.length > 0 ? root.currentFolderPath : "Mounted sources appear on the left."
                                    color: "#f0d2a0"
                                    font.family: "Menlo"
                                    font.pixelSize: 10
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            RowLayout {
                                spacing: 8

                                ActionButton {
                                    text: "Up One Folder"
                                    enabled: root.currentFolderPath.length > 0 && root.currentFolderPath !== root.sourceRootPath
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

                        Label {
                            text: catalogAssets.status
                            color: "#8d98a3"
                            font.family: "Avenir Next"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 230
                        Layout.fillHeight: true
                        radius: 14
                        color: "#10151a"
                        border.width: 1
                        border.color: "#232c34"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            Label {
                                text: "Folders"
                                color: "#f2f4f6"
                                font.family: "Avenir Next"
                                font.pixelSize: 15
                                font.weight: Font.Medium
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 8
                                model: directoryModel

                                delegate: Rectangle {
                                    required property int index
                                    required property string fileName
                                    required property string fileUrl

                                    width: ListView.view.width
                                    height: 48
                                    radius: 10
                                    color: "#141b21"
                                    border.width: 1
                                    border.color: "#222a32"

                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 12
                                        anchors.right: parent.right
                                        anchors.rightMargin: 12
                                        text: fileName
                                        color: "#dde3e8"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.openFolder(fileUrl)
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 14
                        color: "#10151a"
                        border.width: 1
                        border.color: "#232c34"

                        Item {
                            anchors.fill: parent
                            anchors.margins: 10

                            GridView {
                                anchors.fill: parent
                                clip: true
                                cellWidth: 188
                                cellHeight: 218
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
                                    readonly property bool selected: !!root.selectedPathsByFile[filePath]
                                    readonly property string delegateExtension: {
                                        const parts = delegateFileName.split(".")
                                        return parts.length > 1 ? parts[parts.length - 1].toUpperCase() : "FILE"
                                    }

                                    width: 176
                                    height: 206
                                    radius: 12
                                    color: selected ? "#192128" : "#12181d"
                                    border.width: selected ? 1 : 0
                                    border.color: "#d0a157"

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 8

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 150
                                            radius: 10
                                            color: "#0d1216"
                                            clip: true

                                            Image {
                                                id: previewImage
                                                anchors.fill: parent
                                                source: delegateFileUrl
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                                cache: false
                                                visible: status === Image.Ready
                                            }

                                            Column {
                                                anchors.centerIn: parent
                                                visible: previewImage.status !== Image.Ready
                                                spacing: 6

                                                Label {
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    text: delegateExtension
                                                    color: "#f1d5a6"
                                                    font.family: "Menlo"
                                                    font.pixelSize: 16
                                                    font.weight: Font.DemiBold
                                                }

                                                Label {
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    text: "Preview unavailable"
                                                    color: "#90a0ac"
                                                    font.family: "Avenir Next"
                                                    font.pixelSize: 10
                                                }
                                            }

                                            Rectangle {
                                                anchors.top: parent.top
                                                anchors.right: parent.right
                                                anchors.topMargin: 8
                                                anchors.rightMargin: 8
                                                width: 24
                                                height: 24
                                                radius: 12
                                                color: selected ? "#c99a52" : Qt.rgba(17 / 255, 22 / 255, 27 / 255, 0.72)
                                                border.width: 1
                                                border.color: selected ? "#f3d6aa" : "#3b444c"

                                                Label {
                                                    anchors.centerIn: parent
                                                    text: selected ? "✓" : "+"
                                                    color: selected ? "#111317" : "#dbe1e7"
                                                    font.family: "Avenir Next"
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                }
                                            }
                                        }

                                        Label {
                                            text: delegateFileName
                                            color: "#f2f5f7"
                                            font.family: "Avenir Next"
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: root.parentPathForFile(delegateFilePath)
                                            color: "#7f8a94"
                                            font.family: "Avenir Next"
                                            font.pixelSize: 10
                                            elide: Text.ElideMiddle
                                            Layout.fillWidth: true
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.toggleSelection(delegateFilePath)
                                    }
                                }
                            }

                            Column {
                                anchors.centerIn: parent
                                visible: root.currentFolderPath.length > 0 && fileModel.count === 0
                                spacing: 8

                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "No supported photos here."
                                    color: "#d7dde3"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 18
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Open another folder or choose a different source."
                                    color: "#85909a"
                                    font.family: "Avenir Next"
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            radius: 18
            color: "#14191d"
            border.width: 1
            border.color: "#242c34"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Label {
                    text: "Import Queue"
                    color: "#f2f4f6"
                    font.family: "Avenir Next"
                    font.pixelSize: 18
                    font.weight: Font.Medium
                }

                Label {
                    text: root.selectedFileCount > 0
                          ? root.selectedFileCount + " files ready from " + (root.sourceLabel.length > 0 ? root.sourceLabel : "the current source")
                          : "Select one or many files, or import the current folder recursively."
                    color: "#9ca6af"
                    font.family: "Avenir Next"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
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
                    currentIndex: root.projectOptionValues.indexOf(root.projectTarget)
                    onActivated: root.projectTarget = root.projectOptionValues[currentIndex]
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ActionButton {
                        Layout.fillWidth: true
                        text: "Create"
                        enabled: newProjectField.text.trim().length > 0
                        onClicked: {
                            catalogAssets.createProject(newProjectField.text)
                            newProjectField.text = ""
                        }
                    }

                    ActionButton {
                        Layout.fillWidth: true
                        text: projectTarget.length > 0 ? projectTarget : "No Project"
                        enabled: false
                    }
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: catalogAssets.importing ? "Importing..." : "Import"
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
                    radius: 12
                    color: "#10151a"
                    border.width: 1
                    border.color: "#222a32"

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 10
                        clip: true

                        Column {
                            width: parent.width
                            spacing: 8

                            Repeater {
                                model: root.selectedFilePaths

                                delegate: Rectangle {
                                    required property string modelData

                                    width: parent.width
                                    height: 54
                                    radius: 10
                                    color: "#141b21"
                                    border.width: 1
                                    border.color: "#222a32"

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 2

                                        Label {
                                            text: root.fileNameForPath(modelData)
                                            color: "#f2f5f7"
                                            font.family: "Avenir Next"
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            width: parent.width
                                        }

                                        Label {
                                            text: root.parentPathForFile(modelData)
                                            color: "#7f8a94"
                                            font.family: "Avenir Next"
                                            font.pixelSize: 10
                                            elide: Text.ElideMiddle
                                            width: parent.width
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.toggleSelection(modelData)
                                    }
                                }
                            }

                            Label {
                                visible: root.selectedFileCount === 0
                                text: "Selected photos will appear here. Assigned project membership is optional."
                                color: "#85909a"
                                font.family: "Avenir Next"
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                width: parent.width
                            }
                        }
                    }
                }
            }
        }
    }
}
