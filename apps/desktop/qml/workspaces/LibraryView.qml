import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel
import "../components"

Rectangle {
    id: root
    color: "transparent"

    Theme { id: theme }

    signal assetActivated(string destination)
    signal localAssetActivated(string filePath, string previewSource, string displayName)

    property string projectTarget: ""
    property bool importBrowserOpen: false
    property string localFolderUrl: ""
    property string localFolderPath: ""
    property string localSelectedFilePath: ""

    readonly property var importFilterValues: ["all", "today", "7d", "30d", "year"]
    readonly property var importFilterLabels: ["All Imports", "Today", "Last 7 Days", "Recent", "This Year"]
    readonly property var mediaFilterValues: ["all", "photos", "videos", "timelines"]
    readonly property var mediaFilterLabels: ["All Media", "Photos", "Videos", "Timelines"]
    readonly property var captureFilterValues: {
        const values = ["all", "unknown"]
        for (let i = 0; i < catalogAssets.captureYears.length; ++i)
            values.push(catalogAssets.captureYears[i])
        return values
    }
    readonly property var captureFilterLabels: {
        const labels = ["All Capture Dates", "Unknown"]
        for (let i = 0; i < catalogAssets.captureYears.length; ++i)
            labels.push(catalogAssets.captureYears[i])
        return labels
    }
    readonly property var projectFilterValues: {
        const values = ["all", "unassigned"]
        for (let i = 0; i < catalogAssets.projectNames.length; ++i)
            values.push(catalogAssets.projectNames[i])
        return values
    }
    readonly property var projectFilterLabels: {
        const labels = ["All Projects", "Unassigned"]
        for (let i = 0; i < catalogAssets.projectNames.length; ++i)
            labels.push(catalogAssets.projectNames[i])
        return labels
    }

    function indexForValue(values, value) {
        const index = values.indexOf(value)
        return index >= 0 ? index : 0
    }

    function activeFilterSummary() {
        const parts = []
        if (catalogAssets.importDateFilter !== "all")
            parts.push(importFilterLabels[indexForValue(importFilterValues, catalogAssets.importDateFilter)])
        if (catalogAssets.captureYearFilter !== "all")
            parts.push(captureFilterLabels[indexForValue(captureFilterValues, catalogAssets.captureYearFilter)])
        if (catalogAssets.projectFilter !== "all")
            parts.push(projectFilterLabels[indexForValue(projectFilterValues, catalogAssets.projectFilter)])
        if (catalogAssets.mediaTypeFilter !== "all")
            parts.push(mediaFilterLabels[indexForValue(mediaFilterValues, catalogAssets.mediaTypeFilter)])
        return parts.length > 0 ? parts.join("  •  ") : "Showing all imported assets"
    }

    function selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType) {
        catalogAssets.selectIndex(index)
        if (mediaType === "photo")
            editorPreview.loadAsset(assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson)
    }

    function chooseImportFolder() {
        const folderUrl = catalogAssets.pickFolder("Browse Media Folder")
        if (folderUrl.length === 0)
            return

        openLocalFolder(folderUrl)
        importBrowserOpen = true
    }

    function openLocalFolder(folderUrl) {
        if (folderUrl.length === 0)
            return

        localFolderUrl = folderUrl
        localFolderPath = catalogAssets.localPathForUrl(folderUrl)
        localSelectedFilePath = ""
    }

    function selectLocalFile(index) {
        localSelectedFilePath = localImageModel.get(index, "filePath")
    }

    function importCurrentFolder() {
        if (localFolderPath.length === 0)
            return

        catalogAssets.importFolderPath(localFolderPath)
        importBrowserOpen = false
    }

    function clearFilters() {
        catalogAssets.importDateFilter = "all"
        catalogAssets.captureYearFilter = "all"
        catalogAssets.projectFilter = "all"
        catalogAssets.mediaTypeFilter = "all"
    }

    function syncProjectTarget() {
        if (catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned" && catalogAssets.projectFilter.length > 0) {
            projectTarget = catalogAssets.projectFilter
            return
        }

        if (catalogAssets.projectNames.length === 0) {
            projectTarget = ""
            return
        }

        if (projectTarget.length === 0 || catalogAssets.projectNames.indexOf(projectTarget) === -1)
            projectTarget = catalogAssets.projectNames[0]
    }

    Component.onCompleted: syncProjectTarget()

    Connections {
        target: catalogAssets

        function onProjectNamesChanged() {
            root.syncProjectTarget()
        }

        function onFiltersChanged() {
            root.syncProjectTarget()
        }

        function onImportingChanged() {
            if (catalogAssets.importing)
                root.importBrowserOpen = false
        }
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
                    Layout.preferredHeight: 148
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
                                    text: catalogAssets.loading ? "Refreshing..." : "Reload"
                                    enabled: !catalogAssets.busy
                                    onClicked: catalogAssets.refresh()
                                }

                                ActionButton {
                                    text: "Clear Filters"
                                    enabled: catalogAssets.importDateFilter !== "all"
                                             || catalogAssets.captureYearFilter !== "all"
                                             || catalogAssets.projectFilter !== "all"
                                             || catalogAssets.mediaTypeFilter !== "all"
                                    onClicked: root.clearFilters()
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

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
                                text: "Project"
                                color: "#7f8a94"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                            }

                            ToneComboBox {
                                Layout.preferredWidth: 200
                                model: root.projectFilterLabels
                                currentIndex: root.indexForValue(root.projectFilterValues, catalogAssets.projectFilter)
                                onActivated: catalogAssets.projectFilter = root.projectFilterValues[currentIndex]
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

                            Item { Layout.fillWidth: true }

                            Label {
                                text: catalogAssets.status
                                color: "#8d98a3"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.maximumWidth: 260
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

                                    width: galleryFlow.tileWidth
                                    height: previewFrame.height

                                    Rectangle {
                                        id: previewFrame
                                        width: parent.width
                                        height: Math.max(120, previewImage.paintedHeight > 0 ? previewImage.paintedHeight : width * 0.66)
                                        radius: 8
                                        color: "#0f1418"
                                        border.width: catalogAssets.selectedIndex === index ? 2 : 0
                                        border.color: "#d0a157"
                                        clip: true

                                        Image {
                                            id: previewImage
                                            anchors.fill: parent
                                            source: thumbnailSource
                                            asynchronous: true
                                            cache: false
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize.width: width
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
                                                root.selectCatalogAsset(index, assetId, filePath, thumbnailSource, cameraLabel, captureDate, lensModel, recipeJson, mediaType)
                                                root.assetActivated(mediaType === "photo" ? "edit" : "timeline")
                                            }
                                        }
                                    }
                                }
                            }
                        }
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
                                text: catalogAssets.totalAssetCount === 0
                                      ? "Switch to Import to bring photos and videos in from folders, cards, and drives."
                                      : "Try changing the media, imported date, capture date, or project filter."
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
                                        text: root.localFolderPath.length > 0
                                              ? root.localFolderPath
                                              : "Choose a folder, preview the media, then import it."
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
            Layout.preferredWidth: 264
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
                    text: "Selection"
                    color: "#f2f4f6"
                    font.family: "Avenir Next"
                    font.pixelSize: 18
                    font.weight: Font.Medium
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 170
                    radius: theme.radius8
                    color: theme.ink900
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: catalogAssets.selectedThumbnailSource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        visible: catalogAssets.selectedThumbnailSource.length > 0
                    }
                }

                Label {
                    text: catalogAssets.selectedCameraLabel.length > 0 ? catalogAssets.selectedCameraLabel : "No asset selected"
                    color: "#f2f5f7"
                    font.family: "Avenir Next"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Label {
                    text: catalogAssets.selectedCaptureDate.length > 0
                          ? catalogAssets.selectedCaptureDate
                          : catalogAssets.selectedMediaType === "timeline"
                            ? "Saved timeline asset."
                            : catalogAssets.selectedMediaType === "video"
                              ? "Video clip selected from the gallery."
                              : "Choose an asset from the grid."
                    color: "#9ca6af"
                    font.family: "Avenir Next"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }

                Label {
                    text: catalogAssets.selectedDurationText
                    visible: text.length > 0
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    Layout.fillWidth: true
                }

                Label {
                    text: catalogAssets.selectedImportedAt.length > 0
                          ? (catalogAssets.selectedMediaType === "timeline" ? "Saved " : "Imported ") + catalogAssets.selectedImportedAt
                          : ""
                    visible: text.length > 0
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    Layout.fillWidth: true
                }

                Label {
                    text: catalogAssets.selectedLocationPath.length > 0 ? catalogAssets.selectedLocationPath : ""
                    visible: text.length > 0
                    color: "#79848d"
                    font.family: "Avenir Next"
                    font.pixelSize: 11
                    wrapMode: Text.WrapAnywhere
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
                    model: catalogAssets.projectNames
                    enabled: catalogAssets.projectNames.length > 0
                    currentIndex: enabled ? Math.max(0, catalogAssets.projectNames.indexOf(root.projectTarget)) : -1
                    onActivated: root.projectTarget = currentText
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
                        text: "Add to Project"
                        tone: "primary"
                        enabled: catalogAssets.selectedAssetId.length > 0 && root.projectTarget.length > 0
                        onClicked: catalogAssets.addAssetToProject(catalogAssets.selectedAssetId, root.projectTarget)
                    }
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: "Remove from Project"
                    enabled: catalogAssets.selectedAssetId.length > 0
                             && root.projectTarget.length > 0
                             && catalogAssets.assetInProject(catalogAssets.selectedAssetId, root.projectTarget)
                    onClicked: catalogAssets.removeAssetFromProject(catalogAssets.selectedAssetId, root.projectTarget)
                }

                Item { Layout.fillHeight: true }

                ActionButton {
                    Layout.fillWidth: true
                    text: catalogAssets.selectedMediaType === "photo" ? "Open in Edit" : "Open in Timeline"
                    enabled: catalogAssets.selectedAssetId.length > 0
                    onClicked: root.assetActivated(catalogAssets.selectedMediaType === "photo" ? "edit" : "timeline")
                }
            }
        }
    }
}
