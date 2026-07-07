pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "galleryPickerDialog"

    required property var theme
    property string dialogTitle: "Gallery"
    property string subtitle: ""
    property string emptyStateText: "No media is available yet."
    property string selectionHintText: "Choose media"
    property bool showProjectScopeToggle: false
    property string scope: "all"
    property var assets: []
    property Component sidePanelFooter
    readonly property bool hasSelection: selectedIndex >= 0 && selectedIndex < assets.length
    readonly property var selectedAsset: hasSelection ? assets[selectedIndex] : ({})
    readonly property string selectedAssetId: hasSelection ? String(selectedAsset.assetId || "") : ""
    readonly property string selectedDisplayLabel: hasSelection ? String(selectedAsset.cameraLabel || selectedAsset.assetId || "") : ""
    readonly property string selectedThumbnailSource: hasSelection ? String(selectedAsset.thumbnailSource || "") : ""
    readonly property string selectedMediaType: hasSelection ? String(selectedAsset.mediaType || "") : ""
    readonly property string selectedDurationText: hasSelection ? String(selectedAsset.durationText || "") : ""
    readonly property string selectedFilePath: hasSelection ? String(selectedAsset.filePath || "") : ""
    readonly property string selectedTimelineDocumentPath: hasSelection ? String(selectedAsset.timelineDocumentPath || "") : ""
    readonly property bool selectedOnline: hasSelection ? Boolean(selectedAsset.online) : false
    readonly property string dialogVisibilityContract: "dialog-visibility:v1|gallery-picker|always:title-scope-grid-selection-panel-close|contextual:selected-preview-offline-warning-side-footer-empty-state|one-expansion:project-scope-all-media-asset-activation|hover-focus:asset-card-selection-keyboard|diagnostic:offline-path-detail|preserve:functionality"
    readonly property string dialogSurfaceContract: "dialog-surface:v1|gallery-picker|grid:dominant|scope:header|selection-panel:always|preview:contextual|footer:contextual|cards:shared-readable|empty-state:bounded-card|no-elide:true|no-overlap:true"
    readonly property string functionPreservationMap: "function-preservation:v1|select-asset:grid-card|activate-asset:double-click-or-enter|scope-project:header-toggle|scope-all:header-toggle|offline-state:selection-panel|side-footer:contextual-loader"

    signal projectScopeRequested()
    signal allMediaScopeRequested()
    signal assetActivated(var asset)

    property int selectedIndex: -1

    function gcd(a, b) {
        let left = Math.abs(Math.round(a))
        let right = Math.abs(Math.round(b))
        while (right > 0) {
            const next = left % right
            left = right
            right = next
        }
        return Math.max(1, left)
    }

    function aspectRatioText(width, height) {
        if (width <= 0 || height <= 0)
            return ""
        const divisor = gcd(width, height)
        return Math.round(width / divisor) + ":" + Math.round(height / divisor)
    }

    function clearSelection() {
        selectedIndex = -1
    }

    function selectAssetIndex(index) {
        if (index < 0 || index >= assets.length) {
            clearSelection()
            return
        }
        selectedIndex = index
    }

    function openDialog() {
        if (assets.length > 0)
            selectedIndex = 0
        else
            clearSelection()
        open()
    }

    onAssetsChanged: {
        if (assets.length === 0) {
            clearSelection()
            return
        }
        if (selectedIndex < 0 || selectedIndex >= assets.length)
            selectedIndex = 0
    }

    parent: Overlay.overlay
    modal: true
    width: Math.min((parent ? parent.width : 1112) - 32, 1080)
    height: Math.min((parent ? parent.height : 792) - 32, 760)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: PopupSurface {
        radius: 22
        accent: root.theme.accentWarm400
        accentRail: false
    }

    contentItem: RowLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: root.dialogTitle
                            color: root.theme.textStrong
                            font.family: root.theme.fontDisplay
                            font.pixelSize: 26
                            font.weight: Font.DemiBold
                        }

                        Label {
                            text: root.subtitle
                            color: root.theme.textMuted
                            font.family: root.theme.fontUi
                            font.pixelSize: 11
                        }
                    }

                    ActionButton {
                        text: "Current Project"
                        compact: true
                        active: root.scope === "project"
                        visible: root.showProjectScopeToggle
                        onClicked: root.projectScopeRequested()
                    }

                    ActionButton {
                        text: "All Media"
                        compact: true
                        active: root.scope === "all"
                        onClicked: root.allMediaScopeRequested()
                    }

                    ActionButton {
                        text: "Close"
                        onClicked: root.close()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 18
                    color: root.theme.alpha(root.theme.ink950, 0.76)
                    border.width: 1
                    border.color: root.theme.alpha(root.theme.lineSoft, 0.4)
                    clip: true

                    GridView {
                        id: mediaGrid
                        anchors.fill: parent
                        anchors.margins: 12
                        clip: true
                        cellWidth: 176
                        cellHeight: 212
                        model: root.assets

                        delegate: GalleryAssetCard {
                            required property int index
                            required property var modelData

                            assetIndex: index
                            asset: modelData
                            selected: root.selectedAssetId === String(modelData.assetId || "")
                            onSelectedRequested: function(assetIndex) {
                                root.selectAssetIndex(assetIndex)
                            }
                            onActivated: function(assetIndex) {
                                root.selectAssetIndex(assetIndex)
                                root.assetActivated(root.selectedAsset)
                            }
                        }
                    }

                    Rectangle {
                        id: galleryEmptyStateCard
                        objectName: "galleryPickerEmptyStateCard"
                        readonly property string emptyStateContract: "gallery-empty-state:v1|bounded-card:true|icon-zone:true|text-wrap:true|max-lines:3|no-elide:true|no-overlap:true"
                        anchors.centerIn: parent
                        visible: root.assets.length === 0
                        width: Math.min(parent.width - 40, Math.max(240, galleryEmptyStateContent.implicitWidth + 32))
                        height: galleryEmptyStateContent.implicitHeight + 28
                        radius: 14
                        color: root.theme.alpha(root.theme.ink900, 0.84)
                        border.width: 1
                        border.color: root.theme.alpha(root.theme.lineSoft, 0.38)

                        ColumnLayout {
                            id: galleryEmptyStateContent
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                                radius: 14
                                color: root.theme.alpha(root.theme.accentCool400, 0.14)
                                border.width: 1
                                border.color: root.theme.alpha(root.theme.accentCool400, 0.34)

                                AppIcon {
                                    anchors.centerIn: parent
                                    width: 14
                                    height: 14
                                    name: "media"
                                    color: root.theme.textMuted
                                }
                            }

                            Label {
                                objectName: "galleryPickerEmptyStateLabel"
                                Layout.fillWidth: true
                                text: root.emptyStateText
                                color: root.theme.textMuted
                                font.family: root.theme.fontUi
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: 3
                                elide: Text.ElideNone
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: root.theme.alpha(root.theme.ink950, 0.84)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Label {
                    text: "Selection"
                    color: root.theme.textStrong
                    font.family: root.theme.fontUi
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 170
                    radius: 14
                    color: "#0b1014"
                    border.width: 1
                    border.color: root.theme.alpha(root.theme.lineSoft, 0.34)
                    clip: true

                    Image {
                        id: selectedPreview
                        anchors.fill: parent
                        source: root.selectedThumbnailSource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        visible: root.selectedThumbnailSource.length > 0
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: root.selectedThumbnailSource.length === 0
                        text: root.selectionHintText
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: 11
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.selectedDisplayLabel.length > 0 ? root.selectedDisplayLabel : "No media selected"
                    color: root.theme.textStrong
                    font.family: root.theme.fontUi
                    font.pixelSize: 16
                    font.weight: Font.Medium
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: root.selectedMediaType === "video"
                          ? "Video clip selected."
                          : root.selectedMediaType === "audio"
                            ? "Audio clip selected."
                            : root.selectedMediaType === "photo"
                              ? "Photo selected."
                              : root.selectedMediaType === "timeline"
                                ? "Saved timeline selected."
                                : "Select media from the gallery."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: root.aspectRatioText(selectedPreview.sourceSize.width, selectedPreview.sourceSize.height)
                          + (root.aspectRatioText(selectedPreview.sourceSize.width, selectedPreview.sourceSize.height).length > 0 ? " aspect" : "")
                    visible: text.length > 0
                    color: root.theme.textMuted
                    font.family: root.theme.fontMono
                    font.pixelSize: 10
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.hasSelection && !root.selectedOnline
                    text: "This media is offline and cannot be loaded until the source path is available."
                    color: "#f4b7af"
                    font.family: root.theme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    text: root.selectedDurationText
                    visible: text.length > 0
                    color: root.theme.textMuted
                    font.family: root.theme.fontMono
                    font.pixelSize: 10
                }

                Loader {
                    Layout.fillWidth: true
                    active: root.sidePanelFooter !== undefined && root.sidePanelFooter !== null
                    sourceComponent: root.sidePanelFooter
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
