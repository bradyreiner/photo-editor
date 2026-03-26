import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "workspaces"

ApplicationWindow {
    id: window
    width: 1600
    height: 980
    visible: true
    title: "PhotoEditor"
    color: theme.ink950
    font.family: theme.fontUi
    font.pixelSize: 12

    Theme { id: theme }

    property int currentWorkspace: 0
    property string libraryWorkspaceMode: "catalog"
    property string editWorkspaceMode: "adjust"
    property bool editorUsesLocalAsset: false
    property string localEditorFilePath: ""
    property string localEditorPreviewSource: ""
    property string localEditorDisplayName: ""
    readonly property int editWorkspaceIndex: 1
    readonly property int carouselWorkspaceIndex: 2
    readonly property int timelineWorkspaceIndex: 3
    readonly property int exportWorkspaceIndex: 4
    readonly property var workspaceTitles: ["Library", "Edit", "Carousel", "Timeline", "Export"]
    readonly property var workspaceKinds: ["library", "edit", "carousel", "timeline", "export"]
    readonly property var workspaceSubtitles: [
        "Browse imported assets or bring media into the catalog.",
        "Refine a single image and handle local masking without leaving the canvas.",
        "Assemble social slides and mixed-media carousel layouts.",
        "Cut video on a dedicated editing timeline.",
        "Prepare deliveries and export jobs."
    ]
    readonly property var workspaceDetails: [
        "Catalog and intake",
        "Adjust and mask",
        "Carousel composer",
        "Timeline editor",
        "Delivery pipeline"
    ]

    function editorAssetReady() {
        return catalogAssets.selectedAssetId.length > 0
                || (editorUsesLocalAsset && localEditorFilePath.length > 0)
    }

    function workspaceBadge(index) {
        if (index === 0)
            return String(catalogAssets.totalAssetCount)

        if (index === editWorkspaceIndex)
            return editorAssetReady() ? "Ready" : ""

        if (index === carouselWorkspaceIndex)
            return carouselDocument.slideCount > 0 ? String(carouselDocument.slideCount) : ""

        if (index === timelineWorkspaceIndex)
            return timelineDocument.clipCount > 0 ? String(timelineDocument.clipCount) : ""

        return ""
    }

    function selectionHeadline() {
        if (editorUsesLocalAsset && localEditorDisplayName.length > 0)
            return localEditorDisplayName

        if (catalogAssets.selectedAssetId.length > 0)
            return catalogAssets.selectedCameraLabel

        return "No active asset"
    }

    function selectionMeta() {
        if (editorUsesLocalAsset && localEditorDisplayName.length > 0)
            return "Local image  •  Ready in Edit"

        if (catalogAssets.selectedAssetId.length > 0) {
            const parts = []
            if (catalogAssets.selectedMediaType === "photo" && catalogAssets.selectedCaptureDate.length > 0)
                parts.push(catalogAssets.selectedCaptureDate)
            if (catalogAssets.selectedMediaType !== "photo")
                parts.push(catalogAssets.selectedMediaType === "timeline" ? "Saved timeline" : "Video clip")
            if (catalogAssets.selectedDurationText.length > 0)
                parts.push(catalogAssets.selectedDurationText)
            if (catalogAssets.selectedLensModel.length > 0)
                parts.push(catalogAssets.selectedLensModel)
            return parts.length > 0 ? parts.join("  •  ") : "Catalog asset"
        }

        return "Choose an asset in Library to drive Edit, Carousel, or Timeline."
    }

    function selectionSupport() {
        if (editorUsesLocalAsset && localEditorFilePath.length > 0)
            return localEditorFilePath

        if (catalogAssets.selectedAssetId.length > 0) {
            if (catalogAssets.selectedProjectSummary.length > 0)
                return catalogAssets.selectedProjectSummary
            if (catalogAssets.selectedImportedAt.length > 0)
                return (catalogAssets.selectedMediaType === "timeline" ? "Saved " : "Imported ") + catalogAssets.selectedImportedAt
            return catalogAssets.selectedMediaType === "photo" ? "Ready for Edit" : "Ready for Timeline"
        }

        return catalogAssets.status
    }

    function loadSelectedAssetIntoEditor() {
        if (catalogAssets.selectedAssetId.length === 0 || catalogAssets.selectedMediaType !== "photo") {
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
        if (currentWorkspace === editWorkspaceIndex) {
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
            if (window.currentWorkspace === window.editWorkspaceIndex
                    && catalogAssets.selectedMediaType === "photo")
                window.loadSelectedAssetIntoEditor();
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.alpha(theme.ink700, 0.22) }
            GradientStop { position: 0.18; color: theme.ink900 }
            GradientStop { position: 1.0; color: theme.ink950 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            Rectangle {
                Layout.preferredWidth: 104
                Layout.fillHeight: true
                radius: theme.radius12
                color: theme.ink850
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 6

                    Repeater {
                        model: workspaceTitles

                        delegate: SidebarButton {
                            required property int index
                            required property string modelData

                            Layout.fillWidth: true
                            title: modelData
                            tooltipText: modelData + "  •  " + workspaceSubtitles[index]
                            badgeText: window.workspaceBadge(index)
                            badgeEmphasis: false
                            compact: false
                            kind: window.workspaceKinds[index]
                            highlighted: index === window.currentWorkspace
                            onClicked: window.currentWorkspace = index
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Button {
                        id: compactStatusButton
                        Layout.alignment: Qt.AlignLeft
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        hoverEnabled: true

                        ToolTip.visible: compactStatusButton.hovered
                        ToolTip.delay: 250
                        ToolTip.text: window.selectionHeadline() + "\n" + window.selectionMeta()

                        background: Rectangle {
                            radius: theme.radius8
                            color: compactStatusButton.hovered ? theme.alpha(theme.ink700, 0.18) : "transparent"
                        }

                        contentItem: RowLayout {
                            spacing: 6

                            Rectangle {
                                implicitWidth: 6
                                implicitHeight: 6
                                radius: 3
                                color: (window.editorAssetReady() || catalogAssets.selectedAssetId.length > 0) ? theme.textMuted : theme.textFaint
                            }

                            Label {
                                Layout.fillWidth: true
                                text: window.selectionHeadline()
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: theme.radius12
                color: theme.ink900
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.3)

                Loader {
                    anchors.fill: parent
                    anchors.margins: 4
                    sourceComponent: [libraryView, editView, carouselView, timelineView, exportView][window.currentWorkspace]
                }
            }
        }
    }

    Component {
        id: libraryView

        LibraryWorkspace {
            mode: window.libraryWorkspaceMode
            onModeSelected: function(mode) {
                window.libraryWorkspaceMode = mode
            }
            onAssetActivated: function(destination) {
                if (destination === "edit") {
                    window.editWorkspaceMode = "adjust"
                    window.currentWorkspace = window.editWorkspaceIndex
                    return
                }

                if (catalogAssets.selectedMediaType === "timeline"
                        && catalogAssets.selectedTimelineDocumentPath.length > 0)
                    timelineDocument.loadTimeline(catalogAssets.selectedTimelineDocumentPath)

                window.currentWorkspace = window.timelineWorkspaceIndex
            }
            onLocalAssetActivated: function (filePath, previewSource, displayName) {
                window.editorUsesLocalAsset = true
                window.localEditorFilePath = filePath
                window.localEditorPreviewSource = previewSource
                window.localEditorDisplayName = displayName
                window.loadLocalAssetIntoEditor()
                window.editWorkspaceMode = "adjust"
                window.currentWorkspace = window.editWorkspaceIndex
            }
        }
    }

    Component {
        id: editView

        EditWorkspace {
            mode: window.editWorkspaceMode
            onModeSelected: function(mode) {
                window.editWorkspaceMode = mode
            }
        }
    }

    Component {
        id: carouselView

        CarouselView {}
    }

    Component {
        id: timelineView

        TimelineView {}
    }

    Component { id: exportView; ExportView {} }
}
