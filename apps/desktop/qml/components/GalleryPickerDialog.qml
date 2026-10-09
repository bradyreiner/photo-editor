pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
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
    property Item focusReturnItem: null
    property Item focusFallbackItem: null
    readonly property bool compactHeader: galleryMainPanel.width < root.theme.scaled(520)
    readonly property bool hasSelection: selectedIndex >= 0 && selectedIndex < assets.length
    readonly property var selectedAsset: hasSelection ? assets[selectedIndex] : ({})
    readonly property string selectedAssetId: hasSelection ? String(selectedAsset.assetId || "") : ""
    readonly property string selectedDisplayLabel: hasSelection ? String(selectedAsset.cameraLabel || selectedAsset.assetId || "") : ""
    readonly property string selectedThumbnailSource: hasSelection ? String(selectedAsset.thumbnailSource || "") : ""
    readonly property string selectedMediaType: hasSelection ? String(selectedAsset.mediaType || "") : ""
    readonly property string selectedDurationText: hasSelection ? String(selectedAsset.durationText || "") : ""
    readonly property real selectedDurationSeconds: hasSelection ? Number(selectedAsset.durationSeconds || 0) : 0
    readonly property string selectedFilePath: hasSelection ? String(selectedAsset.filePath || "") : ""
    readonly property string selectedTimelineDocumentPath: hasSelection ? String(selectedAsset.timelineDocumentPath || "") : ""
    readonly property bool selectedOnline: hasSelection ? Boolean(selectedAsset.online) : false
    readonly property string dialogVisibilityContract: "dialog-visibility:v2|gallery-picker|always:title-scope-grid-close|contextual:selection-panel-selected-preview-offline-warning-side-footer-empty-state|one-expansion:project-scope-all-media-asset-activation|hover-focus:asset-card-selection-keyboard|diagnostic:offline-path-detail|preserve:functionality"
    readonly property string dialogSurfaceContract: "dialog-surface:v2|gallery-picker|grid:dominant|scope:header|selection-panel:when-selected|preview:contextual|footer:preserved-in-empty-state|cards:shared-readable|empty-state:flat-guidance|no-elide:true|no-overlap:true"
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
        mediaGrid.currentIndex = index
    }

    function openDialog() {
        if (assets.length > 0) {
            selectedIndex = 0
            mediaGrid.currentIndex = 0
        } else {
            clearSelection()
            mediaGrid.currentIndex = -1
        }
        open()
    }

    function focusInitialTarget() {
        const initialIndex = hasSelection ? selectedIndex : 0
        const initialCard = assets.length > 0 ? mediaGrid.itemAtIndex(initialIndex) : null
        if (initialCard)
            initialCard.forceActiveFocus(Qt.TabFocusReason)
        else
            closeButton.forceActiveFocus(Qt.TabFocusReason)
    }

    onAssetsChanged: {
        if (assets.length === 0) {
            clearSelection()
            mediaGrid.currentIndex = -1
            return
        }
        if (selectedIndex < 0 || selectedIndex >= assets.length) {
            selectedIndex = 0
            mediaGrid.currentIndex = 0
        }
    }

    onAboutToShow: {
        const popupWindow = root.parent ? root.parent.Window.window : null
        if (!root.focusReturnItem && popupWindow)
            root.focusReturnItem = popupWindow.activeFocusItem
        if (!root.focusFallbackItem && popupWindow)
            root.focusFallbackItem = popupWindow.contentItem
    }
    onOpened: initialFocusTimer.restart()
    onClosed: focusRestoreTimer.restart()
    Timer {
        id: initialFocusTimer
        interval: 0
        repeat: false
        onTriggered: root.focusInitialTarget()
    }
    Timer {
        id: focusRestoreTimer
        interval: 1
        repeat: false
        onTriggered: {
            if (root.focusReturnItem && root.focusReturnItem.visible && root.focusReturnItem.enabled) {
                root.focusReturnItem.forceActiveFocus(Qt.TabFocusReason)
            } else if (root.focusFallbackItem && root.focusFallbackItem.visible && root.focusFallbackItem.enabled) {
                root.focusFallbackItem.forceActiveFocus(Qt.TabFocusReason)
            }
            root.focusReturnItem = null
        }
    }

    parent: Overlay.overlay
    modal: true
    focus: true
    width: Math.min((parent ? parent.width : 1112) - 32, 1080)
    height: Math.min((parent ? parent.height : 792) - 32, 760)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: PopupSurface {
        radius: root.theme.radiusPanel
        accent: root.theme.accentWarm400
        accentRail: false
        Accessible.name: root.dialogTitle
        Accessible.description: root.subtitle.length > 0 ? root.subtitle : "Choose media from the gallery."
        Accessible.role: Accessible.Dialog
        Accessible.ignored: false
    }

    contentItem: RowLayout {
        spacing: 0

        Rectangle {
            id: galleryMainPanel
            objectName: "galleryPickerMainPanel"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.compactHeader ? 2 : 4
                    columnSpacing: 10
                    rowSpacing: 8

                    ColumnLayout {
                        objectName: "galleryPickerTitleGroup"
                        Layout.columnSpan: root.compactHeader ? 2 : 1
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            text: root.dialogTitle
                            color: root.theme.textStrong
                            font.family: root.theme.fontDisplay
                            font.pixelSize: root.theme.typeDialogTitle
                            font.weight: Font.DemiBold
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            wrapMode: Text.WordWrap
                            text: root.subtitle
                            color: root.theme.textMuted
                            font.family: root.theme.fontUi
                            font.pixelSize: 11
                        }
                    }

                    ActionButton {
                        objectName: "galleryPickerProjectScopeButton"
                        text: "Current Project"
                        compact: true
                        active: root.scope === "project"
                        visible: root.showProjectScopeToggle
                        onClicked: root.projectScopeRequested()
                    }

                    ActionButton {
                        objectName: "galleryPickerAllMediaScopeButton"
                        text: "All Media"
                        compact: true
                        active: root.scope === "all"
                        onClicked: root.allMediaScopeRequested()
                    }

                    ActionButton {
                        id: closeButton
                        objectName: "galleryPickerCloseButton"
                        Layout.columnSpan: root.compactHeader && root.showProjectScopeToggle ? 2 : 1
                        Layout.alignment: root.compactHeader ? Qt.AlignRight : Qt.AlignVCenter
                        text: "Close"
                        onClicked: root.close()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: root.theme.radiusPanel
                    color: root.theme.alpha(root.theme.ink950, 0.76)
                    border.width: 1
                    border.color: root.theme.alpha(root.theme.lineSoft, 0.4)
                    clip: true

                    GridView {
                        id: mediaGrid
                        objectName: "timelineVirtualizedGalleryGrid"
                        anchors.fill: parent
                        anchors.margins: 12
                        clip: true
                        reuseItems: true
                        cacheBuffer: Math.max(height, 640)
                        cellWidth: 176
                        cellHeight: 212
                        model: root.assets

                        delegate: GalleryAssetCard {
                            required property int index
                            required property var modelData
                            objectName: "galleryPickerAsset-" + String(modelData.assetId || index)

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
                        readonly property string emptyStateContract: "gallery-empty-state:v2|guidance:flat|text-wrap:true|max-lines:3|no-elide:true|no-overlap:true"
                        anchors.centerIn: parent
                        visible: root.assets.length === 0
                        width: Math.max(0, parent.width - 40)
                        height: galleryEmptyStateContent.implicitHeight + 28
                        color: "transparent"

                        ColumnLayout {
                            id: galleryEmptyStateContent
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

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
                Loader {
                    objectName: "galleryPickerEmptyFooter"
                    Layout.fillWidth: true
                    active: !root.hasSelection && root.sidePanelFooter !== undefined && root.sidePanelFooter !== null
                    visible: active
                    sourceComponent: root.sidePanelFooter
                }
            }
        }

        Rectangle {
            objectName: "galleryPickerSelectionPanel"
            visible: root.hasSelection
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
                    radius: root.theme.radiusControl
                    color: root.theme.ink950
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
                    active: root.hasSelection && root.sidePanelFooter !== undefined && root.sidePanelFooter !== null
                    sourceComponent: root.sidePanelFooter
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
