import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: window
    width: 1600
    height: 980
    visible: true
    title: "PhotoEditor"
    color: "#111317"
    font.family: "Avenir Next"
    font.pixelSize: 12

    property int currentWorkspace: 0
    property bool leftSidebarCollapsed: false
    property bool editorUsesLocalAsset: false
    property string localEditorFilePath: ""
    property string localEditorPreviewSource: ""
    property string localEditorDisplayName: ""
    readonly property var workspaceTitles: ["Library", "Edit", "Mask", "Export"]
    readonly property var workspaceKinds: ["library", "edit", "mask", "export"]
    readonly property var workspaceSubtitles: [
        "Browse, import, and select assets.",
        "Refine a single image with a focused preview and inspector.",
        "Build local adjustments against the live image.",
        "Prepare deliveries and export jobs."
    ]
    readonly property var workspaceDetails: [
        "Asset browser",
        "Hero image editor",
        "Local adjustment stack",
        "Delivery pipeline"
    ]

    function editorAssetReady() {
        return catalogAssets.selectedAssetId.length > 0
                || (editorUsesLocalAsset && localEditorFilePath.length > 0)
    }

    function workspaceBadge(index) {
        if (index === 0)
            return String(catalogAssets.totalAssetCount)

        if (index === 1 || index === 2)
            return editorAssetReady() ? "Ready" : ""

        return ""
    }

    function selectionHeadline() {
        if (editorUsesLocalAsset && localEditorDisplayName.length > 0)
            return localEditorDisplayName

        if (catalogAssets.selectedAssetId.length > 0)
            return catalogAssets.selectedCameraLabel

        return "No active image"
    }

    function selectionMeta() {
        if (editorUsesLocalAsset && localEditorDisplayName.length > 0)
            return "Local image  •  Ready in Edit"

        if (catalogAssets.selectedAssetId.length > 0) {
            const parts = []
            if (catalogAssets.selectedCaptureDate.length > 0)
                parts.push(catalogAssets.selectedCaptureDate)
            if (catalogAssets.selectedLensModel.length > 0)
                parts.push(catalogAssets.selectedLensModel)
            return parts.length > 0 ? parts.join("  •  ") : "Catalog image"
        }

        return "Choose an asset in Library to drive Edit and Mask."
    }

    function selectionSupport() {
        if (editorUsesLocalAsset && localEditorFilePath.length > 0)
            return localEditorFilePath

        if (catalogAssets.selectedAssetId.length > 0) {
            if (catalogAssets.selectedProjectSummary.length > 0)
                return catalogAssets.selectedProjectSummary
            if (catalogAssets.selectedImportedAt.length > 0)
                return "Imported " + catalogAssets.selectedImportedAt
            return "Ready for Edit and Mask"
        }

        return catalogAssets.status
    }

    function loadSelectedAssetIntoEditor() {
        if (catalogAssets.selectedAssetId.length === 0) {
            return
        }

        editorPreview.loadAsset(
            catalogAssets.selectedAssetId,
            catalogAssets.selectedFilePath,
            catalogAssets.selectedThumbnailSource,
            catalogAssets.selectedCameraLabel,
            catalogAssets.selectedCaptureDate,
            catalogAssets.selectedLensModel,
            catalogAssets.selectedRecipeJson
        )
    }

    function loadLocalAssetIntoEditor() {
        if (localEditorFilePath.length === 0) {
            return
        }

        editorPreview.loadAsset(
            localEditorFilePath,
            localEditorFilePath,
            localEditorPreviewSource,
            localEditorDisplayName,
            "",
            "",
            ""
        )
    }

    onCurrentWorkspaceChanged: {
        if (currentWorkspace === 1 || currentWorkspace === 2) {
            if (editorUsesLocalAsset)
                loadLocalAssetIntoEditor()
            else
                loadSelectedAssetIntoEditor()
        }
    }

    Connections {
        target: catalogAssets

        function onSelectedAssetChanged() {
            window.editorUsesLocalAsset = false
            if (window.currentWorkspace === 1 || window.currentWorkspace === 2)
                window.loadSelectedAssetIntoEditor();
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#111317"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: window.leftSidebarCollapsed ? 88 : 236
                Layout.fillHeight: true
                radius: 18
                color: "#151a1f"
                border.width: 1
                border.color: "#242d36"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: window.leftSidebarCollapsed ? 100 : 86
                        radius: 16
                        color: "#12171c"
                        border.width: 1
                        border.color: "#222b34"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: window.leftSidebarCollapsed ? 10 : 12
                            spacing: window.leftSidebarCollapsed ? 10 : 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Rectangle {
                                    Layout.preferredWidth: 38
                                    Layout.preferredHeight: 38
                                    radius: 12
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: "#d7ae6d" }
                                        GradientStop { position: 1.0; color: "#8c6a35" }
                                    }

                                    Column {
                                        anchors.centerIn: parent
                                        spacing: 2

                                        Rectangle {
                                            width: 16
                                            height: 3
                                            radius: 1
                                            color: "#101317"
                                        }

                                        Rectangle {
                                            width: 12
                                            height: 3
                                            radius: 1
                                            color: "#101317"
                                        }

                                        Rectangle {
                                            width: 8
                                            height: 3
                                            radius: 1
                                            color: "#101317"
                                        }
                                    }
                                }

                                ColumnLayout {
                                    visible: !window.leftSidebarCollapsed
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        text: "Workspace Rail"
                                        color: "#8e98a2"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 10
                                        font.capitalization: Font.AllUppercase
                                    }

                                    Label {
                                        text: "Photo Editor"
                                        color: "#f3f5f7"
                                        font.family: "Avenir Next"
                                        font.pixelSize: 20
                                        font.weight: Font.DemiBold
                                    }
                                }

                                Button {
                                    id: railToggleButton
                                    Layout.alignment: Qt.AlignTop
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 32
                                    hoverEnabled: true

                                    ToolTip.visible: railToggleButton.hovered
                                    ToolTip.delay: 250
                                    ToolTip.text: window.leftSidebarCollapsed ? "Expand sidebar" : "Collapse sidebar"

                                    background: Rectangle {
                                        radius: 10
                                        color: railToggleButton.down ? "#262d34" : (railToggleButton.hovered ? "#20262d" : "#191f25")
                                        border.width: 1
                                        border.color: "#273038"
                                    }

                                    contentItem: Text {
                                        text: window.leftSidebarCollapsed ? ">" : "<"
                                        color: "#e7ebef"
                                        font.family: "Menlo"
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    onClicked: window.leftSidebarCollapsed = !window.leftSidebarCollapsed
                                }
                            }

                            Label {
                                visible: !window.leftSidebarCollapsed
                                text: catalogAssets.totalAssetCount > 0
                                      ? catalogAssets.totalAssetCount + " photos across " + catalogAssets.driveCount + " drives"
                                      : "Build your library, then move into Edit and Mask."
                                color: "#7f8a94"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 16
                        color: "#12171c"
                        border.width: 1
                        border.color: "#222b34"

                        ColumnLayout {
                            Layout.fillWidth: true
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            Label {
                                visible: !window.leftSidebarCollapsed
                                text: "Workspaces"
                                color: "#86919b"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                            }

                            Repeater {
                                model: workspaceTitles

                                delegate: SidebarButton {
                                    required property int index
                                    required property string modelData

                                    Layout.fillWidth: true
                                    title: modelData
                                    subtitle: workspaceDetails[index]
                                    tooltipText: modelData + "  •  " + workspaceSubtitles[index]
                                    badgeText: window.workspaceBadge(index)
                                    badgeEmphasis: index === 0
                                                   ? catalogAssets.totalAssetCount > 0
                                                   : window.editorAssetReady()
                                    compact: window.leftSidebarCollapsed
                                    kind: window.workspaceKinds[index]
                                    highlighted: index === window.currentWorkspace
                                    onClicked: window.currentWorkspace = index
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        visible: !window.leftSidebarCollapsed
                        Layout.fillWidth: true
                        radius: 16
                        color: "#12171c"
                        border.width: 1
                        border.color: "#222b34"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6

                            Label {
                                text: "Current Image"
                                color: "#8f98a2"
                                font.family: "Avenir Next"
                                font.pixelSize: 10
                                font.capitalization: Font.AllUppercase
                            }

                            Label {
                                text: window.selectionHeadline()
                                color: "#f2f4f6"
                                font.family: "Avenir Next"
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Label {
                                text: window.selectionMeta()
                                color: "#a5afb9"
                                font.family: "Avenir Next"
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: "#242d36"
                            }

                            Label {
                                text: window.selectionSupport()
                                color: window.editorAssetReady() ? "#d8b175" : "#7d8791"
                                font.family: "Menlo"
                                font.pixelSize: 10
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Label {
                        visible: !window.leftSidebarCollapsed
                        Layout.fillWidth: true
                        text: catalogAssets.status
                        color: "#6f7983"
                        font.family: "Avenir Next"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Button {
                        id: compactStatusButton
                        visible: window.leftSidebarCollapsed
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        hoverEnabled: true

                        ToolTip.visible: compactStatusButton.hovered
                        ToolTip.delay: 250
                        ToolTip.text: window.selectionHeadline() + "\n" + window.selectionMeta()

                        background: Rectangle {
                            radius: 12
                            color: compactStatusButton.hovered ? "#1d2329" : "#151b20"
                            border.width: 1
                            border.color: window.editorAssetReady() ? "#3a3427" : "#242d35"
                        }

                        contentItem: Rectangle {
                            implicitWidth: 8
                            implicitHeight: 8
                            radius: 4
                            color: window.editorAssetReady() ? "#c99a52" : "#59636d"
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 12
                color: "#13161a"

                Loader {
                    anchors.fill: parent
                    anchors.margins: 10
                    sourceComponent: [libraryView, editView, maskView, exportView][window.currentWorkspace]
                }
            }
        }
    }

    Component {
        id: libraryView

        LibraryView {
            onAssetActivated: window.currentWorkspace = 1
            onLocalAssetActivated: function (filePath, previewSource, displayName) {
                window.editorUsesLocalAsset = true
                window.localEditorFilePath = filePath
                window.localEditorPreviewSource = previewSource
                window.localEditorDisplayName = displayName
                window.loadLocalAssetIntoEditor()
                window.currentWorkspace = 1
            }
        }
    }

    Component { id: editView; EditView {} }
    Component { id: maskView; MaskView {} }
    Component { id: exportView; ExportView {} }
}
