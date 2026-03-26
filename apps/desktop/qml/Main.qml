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
    property bool editorUsesLocalAsset: false
    property string localEditorFilePath: ""
    property string localEditorPreviewSource: ""
    property string localEditorDisplayName: ""
    readonly property var workspaceTitles: ["Library", "Import", "Edit", "Mask", "Export"]
    readonly property var workspaceKinds: ["library", "import", "edit", "mask", "export"]
    readonly property var workspaceSubtitles: [
        "Browse imported assets and select a working image.",
        "Browse drives, cards, and folders to bring photos in.",
        "Refine a single image with a focused preview and inspector.",
        "Build local adjustments against the live image.",
        "Prepare deliveries and export jobs."
    ]
    readonly property var workspaceDetails: [
        "Asset browser",
        "In-app importer",
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

        if (index === 2 || index === 3)
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
        if (currentWorkspace === 2 || currentWorkspace === 3) {
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
            if (window.currentWorkspace === 2 || window.currentWorkspace === 3)
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
                Layout.preferredWidth: 72
                Layout.fillHeight: true
                radius: 20
                color: "#12171c"
                border.width: 1
                border.color: "#1d242b"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 10

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 10
                    }

                    Repeater {
                        model: workspaceTitles

                        delegate: SidebarButton {
                            required property int index
                            required property string modelData

                            Layout.fillWidth: true
                            title: ""
                            subtitle: ""
                            tooltipText: modelData + "  •  " + workspaceSubtitles[index]
                            badgeText: ""
                            badgeEmphasis: false
                            compact: true
                            kind: window.workspaceKinds[index]
                            highlighted: index === window.currentWorkspace
                            onClicked: window.currentWorkspace = index
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Button {
                        id: compactStatusButton
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        hoverEnabled: true

                        ToolTip.visible: compactStatusButton.hovered
                        ToolTip.delay: 250
                        ToolTip.text: window.selectionHeadline() + "\n" + window.selectionMeta()

                        background: Rectangle {
                            radius: 12
                            color: compactStatusButton.hovered ? "#171d23" : "transparent"
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
                    sourceComponent: [libraryView, importView, editView, maskView, exportView][window.currentWorkspace]
                }
            }
        }
    }

    Component {
        id: libraryView

        LibraryView {
            onAssetActivated: window.currentWorkspace = 2
            onLocalAssetActivated: function (filePath, previewSource, displayName) {
                window.editorUsesLocalAsset = true
                window.localEditorFilePath = filePath
                window.localEditorPreviewSource = previewSource
                window.localEditorDisplayName = displayName
                window.loadLocalAssetIntoEditor()
                window.currentWorkspace = 2
            }
        }
    }

    Component { id: importView; ImportView {} }
    Component { id: editView; EditView {} }
    Component { id: maskView; MaskView {} }
    Component { id: exportView; ExportView {} }
}
