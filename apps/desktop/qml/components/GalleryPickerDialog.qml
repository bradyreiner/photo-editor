pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

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

                        delegate: Rectangle {
                            required property int index
                            required property var modelData

                            readonly property string assetId: String(modelData.assetId || "")
                            readonly property string cameraLabel: String(modelData.cameraLabel || "")
                            readonly property string thumbnailSource: String(modelData.thumbnailSource || "")
                            readonly property string mediaType: String(modelData.mediaType || "")
                            readonly property string durationText: String(modelData.durationText || "")
                            readonly property bool online: Boolean(modelData.online)

                            width: mediaGrid.cellWidth
                            height: mediaGrid.cellHeight
                            radius: 14
                            color: root.selectedAssetId === assetId
                                   ? root.theme.alpha(root.theme.accentWarm500, 0.16)
                                   : root.theme.alpha(root.theme.ink900, 0.88)
                            border.width: 1
                            border.color: root.selectedAssetId === assetId
                                          ? root.theme.accentWarm300
                                          : root.theme.alpha(root.theme.lineSoft, 0.34)
                            opacity: online ? 1 : 0.56

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 132
                                    radius: 10
                                    color: "#0b1014"
                                    clip: true

                                    Image {
                                        id: cardPreview
                                        anchors.fill: parent
                                        source: thumbnailSource
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                        visible: thumbnailSource.length > 0
                                    }

                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 8
                                        visible: cardPreview.status === Image.Ready && root.aspectRatioText(cardPreview.sourceSize.width, cardPreview.sourceSize.height).length > 0
                                        radius: 8
                                        color: root.theme.alpha(root.theme.ink950, 0.88)
                                        border.width: 1
                                        border.color: root.theme.alpha(root.theme.lineSoft, 0.52)
                                        implicitWidth: ratioLabel.implicitWidth + 12
                                        implicitHeight: ratioLabel.implicitHeight + 6

                                        Label {
                                            id: ratioLabel
                                            anchors.centerIn: parent
                                            text: root.aspectRatioText(cardPreview.sourceSize.width, cardPreview.sourceSize.height)
                                            color: root.theme.textStrong
                                            font.family: root.theme.fontMono
                                            font.pixelSize: 10
                                        }
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: thumbnailSource.length === 0
                                        text: mediaType === "timeline" ? "Timeline" : "Preview"
                                        color: root.theme.textMuted
                                        font.family: root.theme.fontUi
                                        font.pixelSize: 11
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: cameraLabel.length > 0 ? cameraLabel : assetId
                                    color: root.theme.textStrong
                                    font.family: root.theme.fontUi
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    Label {
                                        text: mediaType === "photo"
                                              ? "Photo"
                                              : mediaType === "audio"
                                                ? "Audio"
                                                : mediaType === "timeline"
                                                  ? "Timeline"
                                                  : "Video"
                                        color: root.theme.textMuted
                                        font.family: root.theme.fontMono
                                        font.pixelSize: 10
                                    }

                                    Item { Layout.fillWidth: true }

                                    Label {
                                        text: durationText
                                        visible: durationText.length > 0
                                        color: root.theme.textMuted
                                        font.family: root.theme.fontMono
                                        font.pixelSize: 10
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent

                                onClicked: root.selectAssetIndex(index)
                                onDoubleClicked: {
                                    root.selectAssetIndex(index)
                                    root.assetActivated(root.selectedAsset)
                                }
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: root.assets.length === 0
                        text: root.emptyStateText
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: 12
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
