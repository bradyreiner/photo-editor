pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import "../components"

Item {
    id: root

    required property var catalogAssets
    property bool autoAdvance: false
    property string resumeAssetId: ""
    property bool compareMode: false
    property bool fitMode: true
    property real zoomPercent: 100
    property real panX: 0
    property real panY: 0

    signal reviewActionRequested(string action)
    signal ratingRequested(int rating)
    signal clearReviewRequested()
    signal assetSelected(int index)
    signal autoAdvanceToggleRequested()
    signal unmarkedFilterToggleRequested()
    signal resumeRequested()

    Theme { id: theme }
    readonly property var palette: theme

    readonly property int selectedIndex: root.catalogAssets ? Number(root.catalogAssets.selectedIndex) : -1
    readonly property var surveyAssets: root.catalogAssets && selectedIndex >= 0
                                       ? root.catalogAssets.selectedSurveyAssets : []
    readonly property var selectedAsset: {
        const rows = root.surveyAssets || []
        for (let index = 0; index < rows.length; ++index) {
            if (Number(rows[index].visibleIndex) === selectedIndex) {
                const selected = Object.assign({}, rows[index])
                selected.rating = Number(root.catalogAssets.selectedRating || 0)
                selected.picked = Boolean(root.catalogAssets.selectedPicked)
                selected.rejected = Boolean(root.catalogAssets.selectedRejected)
                selected.colorLabel = String(root.catalogAssets.selectedColorLabel || "")
                selected.cullStatus = String(root.catalogAssets.selectedCullStatus || "unmarked")
                return selected
            }
        }
        return ({})
    }
    readonly property var candidates: {
        const rows = root.surveyAssets || []
        if (!rows || rows.length < 2)
            return []
        let selectedPosition = -1
        for (let index = 0; index < rows.length; ++index) {
            if (Number(rows[index].visibleIndex) === selectedIndex) {
                selectedPosition = index
                break
            }
        }
        if (selectedPosition < 0)
            return []
        let comparisonPosition = selectedPosition + 1
        if (comparisonPosition >= rows.length)
            comparisonPosition = selectedPosition - 1
        if (comparisonPosition < 0)
            return []
        return [root.selectedAsset, rows[comparisonPosition]]
    }
    readonly property string selectedName: {
        const path = String(selectedAsset.filePath || "")
        const parts = path.split("/")
        return parts.length > 0 && parts[parts.length - 1].length > 0
                ? parts[parts.length - 1]
                : String(selectedAsset.cameraLabel || selectedAsset.assetId || "Photo")
    }
    readonly property string selectedReviewState: Boolean(selectedAsset.rejected) ? "Rejected"
                                                  : Boolean(selectedAsset.picked) ? "Picked" : "Unmarked"
    readonly property real visibleReviewProgress: root.catalogAssets && root.catalogAssets.assetCount > 0
                                                   ? Math.max(0, Math.min(1,
                                                       (root.catalogAssets.assetCount - root.catalogAssets.visibleUnmarkedCount) /
                                                       root.catalogAssets.assetCount)) : 0
    readonly property string imageQualityLabel: heroImage.status === Image.Ready
        ? (fitMode ? "Fit · original source" : "Original source · " + Math.round(zoomPercent) + "%")
        : heroImage.status === Image.Loading
          ? (String(root.selectedAsset.thumbnailSource || root.catalogAssets.selectedThumbnailSource || "").length > 0
             ? "Loading original · cached preview" : "Loading original source…")
        : String(root.selectedAsset.thumbnailSource || root.catalogAssets.selectedThumbnailSource || "").length > 0
          ? "Cached preview · original unavailable" : "Original preview unavailable"
    readonly property bool originalSourceUnavailable: heroImage.status === Image.Error

    function resetView() {
        fitMode = true
        zoomPercent = 100
        panX = 0
        panY = 0
    }

    function changeZoom(direction) {
        if (fitMode) {
            fitMode = false
            zoomPercent = direction > 0 ? 125 : 75
        } else {
            zoomPercent = Math.max(25, Math.min(400, zoomPercent + direction * 25))
        }
        panX = 0
        panY = 0
    }

    function normalizedImagePath(asset) {
        const path = String((asset && asset.filePath) || "")
        if (path.length === 0)
            return ""
        if (root.catalogAssets && typeof root.catalogAssets.originalPreviewSource === "function") {
            const revisionedSource = String(root.catalogAssets.originalPreviewSource(path) || "")
            if (revisionedSource.length > 0)
                return revisionedSource
        }
        return "image://library-original/source?path=" + encodeURIComponent(path)
    }

    function markStatusText(asset) {
        if (!asset)
            return "Unmarked"
        if (Boolean(asset.rejected))
            return "Rejected"
        if (Boolean(asset.picked))
            return "Picked"
        return "Unmarked"
    }

    Rectangle {
        anchors.fill: parent
        radius: theme.radius12
        color: theme.alpha(theme.ink950, 0.72)
        border.width: theme.chromeBorderWidth
        border.color: theme.alpha(theme.lineSoft, 0.58)
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                objectName: "libraryCullHeader"
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Label {
                        objectName: "libraryCullTitle"
                        text: "Cull photos"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        objectName: "libraryCullProgressLabel"
                        Layout.fillWidth: true
                        text: !root.catalogAssets ? "Library unavailable"
                              : root.catalogAssets.assetCount === 0
                                ? (root.catalogAssets.loading ? "Loading photos…" : "No photos in this view")
                                : (root.catalogAssets.selectedIndex + 1) + " of " + root.catalogAssets.assetCount +
                                  " photos · " + root.catalogAssets.visibleUnmarkedCount + " unmarked"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        elide: Text.ElideNone
                    }
                }

                ColumnLayout {
                    objectName: "libraryCullProgress"
                    visible: root.catalogAssets && root.catalogAssets.assetCount > 0
                    Layout.preferredWidth: root.width < theme.scaled(720)
                                           ? theme.scaled(120)
                                           : root.width < theme.scaled(960) ? theme.scaled(148) : theme.scaled(188)
                    Layout.minimumWidth: root.width < theme.scaled(720) ? theme.scaled(108) : theme.scaled(132)
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "Review progress"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                        }
                        Label {
                            text: root.catalogAssets
                                  ? root.catalogAssets.visiblePickedCount + " pick · " +
                                    root.catalogAssets.visibleRejectedCount + " reject"
                                  : ""
                            color: theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 5
                        radius: 3
                        color: theme.alpha(theme.textMuted, 0.20)

                        Rectangle {
                            width: parent.width * root.visibleReviewProgress
                            height: parent.height
                            radius: 3
                            color: theme.accentWarm400
                            Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                        }
                    }
                }

                MiniButton {
                    objectName: "libraryCullUnmarkedButton"
                    visible: root.catalogAssets && root.catalogAssets.assetCount > 0
                    text: root.catalogAssets ? "Unmarked " + root.catalogAssets.visibleUnmarkedCount : "Unmarked"
                    iconName: "filter"
                    role: "secondary"
                    labelVisible: root.width >= theme.scaled(940)
                    a11yName: "Toggle unmarked photos"
                    active: root.catalogAssets && root.catalogAssets.reviewFilter === "unmarked"
                    tooltip: active ? "Show all review states" : "Show only photos that still need a pick or reject"
                    onClicked: root.unmarkedFilterToggleRequested()
                }

                MiniButton {
                    objectName: "libraryCullResumeButton"
                    visible: root.catalogAssets && root.catalogAssets.assetCount > 0 && root.resumeAssetId.length > 0 &&
                             String(root.selectedAsset.assetId || "") !== root.resumeAssetId
                    text: "Resume"
                    iconName: "history"
                    role: "secondary"
                    labelVisible: root.width >= theme.scaled(940)
                    a11yName: "Resume the last reviewed photo"
                    tooltip: "Return to the last reviewed photo from this shoot"
                    onClicked: root.resumeRequested()
                }

                MiniButton {
                    objectName: "libraryCullCompareButton"
                    visible: root.catalogAssets && root.catalogAssets.assetCount > 0
                    text: "Compare"
                    iconName: "compare"
                    role: "secondary"
                    labelVisible: root.width >= theme.scaled(940)
                    a11yName: "Compare candidates"
                    active: root.compareMode
                    enabled: root.candidates.length === 2
                    disabledReason: "Select a photo with another visible candidate to compare."
                    tooltip: "Compare this photo with its nearest visible candidate"
                    onClicked: {
                        root.compareMode = !root.compareMode
                        root.resetView()
                    }
                }
            }

            RowLayout {
                objectName: "libraryCullReviewRow"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.catalogAssets && root.catalogAssets.assetCount > 0
                spacing: 10

                Rectangle {
                    id: viewer
                    objectName: "libraryCullViewer"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: theme.scaled(260)
                    radius: theme.radius10
                    color: theme.mediaCanvas
                    clip: true

                    Item {
                        id: imageViewport
                        anchors.fill: parent
                        clip: true
                        visible: !root.compareMode

                        ColorManagedImage {
                            id: heroImage
                            objectName: "libraryCullOriginalImage"
                            asynchronous: true
                            cache: false
                            fillMode: Image.PreserveAspectFit
                            imageSource: root.normalizedImagePath(root.selectedAsset)

                            readonly property real fitScale: implicitWidth > 0 && implicitHeight > 0
                                                             ? Math.min(imageViewport.width / implicitWidth,
                                                                        imageViewport.height / implicitHeight)
                                                             : 0
                            readonly property real displayScale: root.fitMode
                                                                 ? fitScale
                                                                 : root.zoomPercent / 100 /
                                                                   Math.max(1, Number(Screen.devicePixelRatio || 1))
                            width: implicitWidth * displayScale
                            height: implicitHeight * displayScale
                            x: (imageViewport.width - width) / 2 + root.panX
                            y: (imageViewport.height - height) / 2 + root.panY

                            onStatusChanged: {
                                if (status === Image.Error)
                                    root.resetView()
                            }
                        }

                        ColorManagedImage {
                            objectName: "libraryCullCachedPreviewImage"
                            anchors.centerIn: parent
                            width: Math.max(1, parent.width - 32)
                            height: Math.max(1, parent.height - 32)
                            imageSource: String(root.selectedAsset.thumbnailSource || root.catalogAssets.selectedThumbnailSource || "")
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: true
                            visible: heroImage.status !== Image.Ready || root.normalizedImagePath(root.selectedAsset).length === 0
                        }

                        OpticalActivityIndicator {
                            objectName: "libraryCullOriginalLoadingIndicator"
                            anchors.centerIn: parent
                            width: 28
                            height: 28
                            active: heroImage.status === Image.Loading
                            visible: active
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: root.normalizedImagePath(root.selectedAsset).length === 0 &&
                                     String(root.selectedAsset.thumbnailSource || root.catalogAssets.selectedThumbnailSource || "").length === 0
                            text: root.catalogAssets && root.catalogAssets.selectedOnline === false
                                  ? "Original is offline"
                                  : "No preview available"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeBody
                        }

                        DragHandler {
                            objectName: "libraryCullPanHandler"
                            target: null
                            enabled: !root.fitMode
                            onActiveChanged: {
                                if (active) {
                                    root._panStartX = root.panX
                                    root._panStartY = root.panY
                                }
                            }
                            onActiveTranslationChanged: {
                                if (active) {
                                    root.panX = root._panStartX + activeTranslation.x
                                    root.panY = root._panStartY + activeTranslation.y
                                }
                            }
                        }

                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: function(event) {
                                root.changeZoom(event.angleDelta.y > 0 ? 1 : -1)
                                event.accepted = true
                            }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 6
                        visible: root.compareMode

                        Repeater {
                            model: root.compareMode ? root.candidates : []

                            delegate: Rectangle {
                                id: candidatePane
                                required property int index
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: theme.radius8
                                color: theme.mediaCanvas
                                clip: true

                                ColorManagedImage {
                                    id: candidateImage
                                    objectName: candidatePane.index === 0 ? "libraryCullCompareImageLeft" : "libraryCullCompareImageRight"
                                    asynchronous: true
                                    cache: false
                                    fillMode: Image.PreserveAspectFit
                                    imageSource: root.normalizedImagePath(candidatePane.modelData)

                                    readonly property real fitScale: implicitWidth > 0 && implicitHeight > 0
                                                                     ? Math.min(parent.width / implicitWidth,
                                                                                parent.height / implicitHeight)
                                                                     : 0
                                    readonly property real displayScale: root.fitMode
                                                                         ? fitScale
                                                                         : root.zoomPercent / 100 /
                                                                           Math.max(1, Number(Screen.devicePixelRatio || 1))
                                    width: implicitWidth * displayScale
                                    height: implicitHeight * displayScale
                                    x: (parent.width - width) / 2 + root.panX
                                    y: (parent.height - height) / 2 + root.panY
                                }

                                ColorManagedImage {
                                    anchors.centerIn: parent
                                    width: Math.max(1, parent.width - 24)
                                    height: Math.max(1, parent.height - 48)
                                    imageSource: String(candidatePane.modelData.thumbnailSource || "")
                                    asynchronous: true
                                    cache: true
                                    fillMode: Image.PreserveAspectFit
                                    visible: candidateImage.status !== Image.Ready || root.normalizedImagePath(candidatePane.modelData).length === 0
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: compareFooterContent.implicitHeight + theme.space16
                                    color: theme.alpha(theme.ink950, 0.86)
                                    border.width: theme.chromeBorderWidth
                                    border.color: theme.alpha(theme.lineSoft, 0.50)

                                    ColumnLayout {
                                        id: compareFooterContent
                                        anchors.fill: parent
                                        anchors.margins: theme.space8
                                        spacing: 2

                                        Label {
                                            Layout.fillWidth: true
                                            text: {
                                                const path = String(candidatePane.modelData.filePath || "")
                                                const parts = path.split("/")
                                                return parts.length > 0 && parts[parts.length - 1].length > 0
                                                        ? parts[parts.length - 1] : "Photo"
                                            }
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            elide: Text.ElideNone
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: root.markStatusText(candidatePane.modelData) + " · " +
                                                  (candidateImage.status === Image.Ready
                                                   ? "Original source"
                                                   : candidateImage.status === Image.Loading
                                                     ? "Loading original · cached preview"
                                                     : candidateImage.status === Image.Error
                                                       ? "Cached preview · original unavailable"
                                                       : "Cached preview")
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeMicro
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        }
                                    }
                                }

                                TapHandler {
                                    onTapped: root.assetSelected(Number(candidatePane.modelData.visibleIndex))
                                }
                            }
                        }

                        DragHandler {
                            objectName: "libraryCullComparePanHandler"
                            target: null
                            enabled: root.compareMode && !root.fitMode
                            onActiveChanged: {
                                if (active) {
                                    root._panStartX = root.panX
                                    root._panStartY = root.panY
                                }
                            }
                            onActiveTranslationChanged: {
                                if (active) {
                                    root.panX = root._panStartX + activeTranslation.x
                                    root.panY = root._panStartY + activeTranslation.y
                                }
                            }
                        }

                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            enabled: root.compareMode
                            onWheel: function(event) {
                                root.changeZoom(event.angleDelta.y > 0 ? 1 : -1)
                                event.accepted = true
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 9
                        radius: 4
                        color: theme.alpha(theme.ink950, 0.84)
                        border.width: theme.chromeBorderWidth
                        border.color: theme.alpha(theme.lineSoft, 0.48)
                        width: qualityLabel.implicitWidth + 16
                        height: qualityLabel.implicitHeight + 10
                        z: 5

                        Label {
                            id: qualityLabel
                            objectName: "libraryCullImageQualityLabel"
                            anchors.centerIn: parent
                            text: root.compareMode
                                  ? (root.fitMode ? "Fit · compare" : Math.round(root.zoomPercent) + "% · synchronized detail")
                                  : root.imageQualityLabel
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                        }
                    }

                    RowLayout {
                        objectName: "libraryCullZoomControls"
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 4
                        z: 5

                        MiniButton {
                            objectName: "libraryCullZoomOutButton"
                            square: true
                            iconName: "minus"
                            role: "icon"
                            tooltip: "Zoom out"
                            onClicked: root.changeZoom(-1)
                        }
                        MiniButton {
                            objectName: "libraryCullZoomFitButton"
                            text: "Fit"
                            role: "secondary"
                            active: root.fitMode
                            tooltip: "Fit the entire source in the viewer"
                            onClicked: root.resetView()
                        }
                        MiniButton {
                            objectName: "libraryCullZoom100Button"
                            text: "100%"
                            role: "secondary"
                            active: !root.fitMode && Math.round(root.zoomPercent) === 100
                            tooltip: "Show one source pixel per display pixel"
                            onClicked: {
                                root.fitMode = false
                                root.zoomPercent = 100
                                root.panX = 0
                                root.panY = 0
                            }
                        }
                        MiniButton {
                            objectName: "libraryCullZoomInButton"
                            square: true
                            iconName: "plus"
                            role: "icon"
                            tooltip: "Zoom in"
                            onClicked: root.changeZoom(1)
                        }
                    }

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        spacing: 2
                        z: 5

                        Label {
                            Layout.fillWidth: true
                            visible: !root.compareMode
                            text: root.selectedName
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                            horizontalAlignment: Text.AlignHCenter
                            Accessible.description: root.selectedAsset.filePath || root.selectedName
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !root.compareMode && String(root.selectedAsset.captureDate || "").length > 0
                            text: String(root.selectedAsset.captureDate || "") +
                                  (String(root.selectedAsset.lensModel || "").length > 0
                                   ? " · " + String(root.selectedAsset.lensModel) : "")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            elide: Text.ElideNone
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                ColumnLayout {
                    objectName: "libraryCullActionPanel"
                    Layout.preferredWidth: theme.scaled(170)
                    Layout.minimumWidth: theme.scaled(170)
                    Layout.maximumWidth: theme.scaled(170)
                    Layout.fillHeight: true
                    spacing: 8

                    Label {
                        text: root.selectedReviewState
                        color: root.selectedReviewState === "Picked" ? theme.success
                               : root.selectedReviewState === "Rejected" ? theme.error : theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeBody
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "P picks · X rejects · U clears review\n0–5 sets stars · ←/→ moves"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }

                    Item { Layout.fillHeight: true }

                    Label {
                        Layout.fillWidth: true
                        text: "Mark"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                        font.weight: Font.DemiBold
                    }

                    MiniButton {
                        objectName: "libraryCullPickButton"
                        Layout.fillWidth: true
                        text: "Pick"
                        iconName: "check"
                        role: "secondary"
                        active: Boolean(root.selectedAsset.picked)
                        accent: theme.success
                        enabled: root.catalogAssets && root.catalogAssets.selectedIndex >= 0
                        onClicked: root.reviewActionRequested("pick")
                    }
                    MiniButton {
                        objectName: "libraryCullRejectButton"
                        Layout.fillWidth: true
                        text: "Reject"
                        iconName: "close"
                        role: "secondary"
                        active: Boolean(root.selectedAsset.rejected)
                        accent: theme.error
                        enabled: root.catalogAssets && root.catalogAssets.selectedIndex >= 0
                        onClicked: root.reviewActionRequested("reject")
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Rating"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                        font.weight: Font.DemiBold
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Repeater {
                            model: 6
                            delegate: MiniButton {
                                objectName: "libraryCullRating" + index + "Button"
                                required property int index
                                Layout.preferredWidth: 26
                                Layout.minimumWidth: 26
                                Layout.maximumWidth: 26
                                Layout.preferredHeight: 26
                                Layout.minimumHeight: 26
                                Layout.maximumHeight: 26
                                square: true
                                iconName: index === 0 ? "minus" : Number(root.selectedAsset.rating || 0) >= index ? "star-gold" : "star"
                                a11yName: index === 0 ? "Set rating to zero stars" : "Set rating to " + index + " stars"
                                role: "icon"
                                active: index > 0 && Number(root.selectedAsset.rating || 0) >= index
                                tooltip: index === 0 ? "Clear rating (0 stars)" : "Set rating to " + index + " stars"
                                enabled: root.catalogAssets && root.catalogAssets.selectedIndex >= 0
                                onClicked: root.ratingRequested(index)
                            }
                        }
                    }
                    MiniButton {
                        objectName: "libraryCullClearReviewButton"
                        Layout.fillWidth: true
                        text: "Clear review"
                        role: "ghost"
                        enabled: root.catalogAssets && root.catalogAssets.selectedIndex >= 0 &&
                                 (Number(root.selectedAsset.rating || 0) > 0 ||
                                  Boolean(root.selectedAsset.picked) || Boolean(root.selectedAsset.rejected) ||
                                  String(root.selectedAsset.colorLabel || "").length > 0)
                        onClicked: root.clearReviewRequested()
                    }
                    MiniButton {
                        objectName: "libraryCullAutoAdvanceButton"
                        Layout.fillWidth: true
                        text: root.autoAdvance ? "Auto advance on" : "Auto advance off"
                        iconName: root.autoAdvance ? "pause" : "play"
                        role: "secondary"
                        active: root.autoAdvance
                        labelVisible: root.width >= theme.scaled(620)
                        a11yName: root.autoAdvance ? "Auto advance on" : "Auto advance off"
                        tooltip: "Move to the next photo after a review mark or rating"
                        onClicked: root.autoAdvanceToggleRequested()
                    }
                }
            }

            ColumnLayout {
                objectName: "libraryCullEmptyState"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.catalogAssets && root.catalogAssets.assetCount === 0
                spacing: theme.space8

                Item { Layout.fillHeight: true }

                OpticalActivityIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: theme.scaled(28)
                    Layout.preferredHeight: theme.scaled(28)
                    active: root.catalogAssets && root.catalogAssets.loading
                }

                Label {
                    Layout.fillWidth: true
                    text: root.catalogAssets && root.catalogAssets.loading ? "Loading photos…" : "No photos to review"
                    color: theme.textStrong
                    font.family: theme.fontDisplay
                    font.pixelSize: theme.typePageTitle
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: theme.scaled(460)
                    text: root.catalogAssets && root.catalogAssets.loading
                          ? "Your current Library view is loading."
                          : "Add or select photos in Library, then return here to mark picks, rejects, and ratings."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    horizontalAlignment: Text.AlignHCenter
                }

                Item { Layout.fillHeight: true }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: theme.alpha(theme.lineSoft, 0.58)
                visible: root.catalogAssets && root.catalogAssets.assetCount > 0
            }

            RowLayout {
                objectName: "libraryCullFilmstripNavigation"
                Layout.fillWidth: true
                Layout.preferredHeight: theme.scaled(102)
                visible: root.catalogAssets && root.catalogAssets.assetCount > 0
                spacing: 8

                MiniButton {
                    objectName: "libraryCullPreviousButton"
                    square: true
                    iconName: "previous"
                    role: "icon"
                    tooltip: "Previous photo (left arrow)"
                    enabled: root.catalogAssets && root.catalogAssets.selectedIndex > 0
                    onClicked: root.catalogAssets.selectRelative(-1)
                }

                ListView {
                    id: filmstrip
                    objectName: "libraryCullFilmstrip"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    spacing: 6
                    clip: true
                    model: root.catalogAssets
                    currentIndex: root.catalogAssets ? root.catalogAssets.selectedIndex : -1
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
                        id: filmstripTile
                        required property int index
                        required property string assetId
                        required property string thumbnailSource
                        required property int rating
                        required property bool picked
                        required property bool rejected
                        width: theme.scaled(112)
                        height: ListView.view.height
                        radius: theme.radius8
                        color: root.catalogAssets && root.catalogAssets.selectedIndex === index
                               ? theme.alpha(theme.accentWarm400, 0.16) : theme.ink900
                        border.width: root.catalogAssets && root.catalogAssets.selectedIndex === index ? 2 : 1
                        border.color: root.catalogAssets && root.catalogAssets.selectedIndex === index
                                      ? theme.accentWarm400 : theme.alpha(theme.lineSoft, 0.52)
                        clip: true

                        ColorManagedImage {
                            id: filmstripThumb
                            anchors.fill: parent
                            anchors.margins: 2
                            imageSource: filmstripTile.thumbnailSource
                            sourceSize: Qt.size(240, 180)
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectFit
                        }

                        Label {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 20
                            text: String(filmstripTile.index + 1)
                            color: theme.textStrong
                            font.family: theme.fontNumeric
                            font.features: theme.numericFontFeatures
                            font.pixelSize: theme.typeMicro
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            background: Rectangle { color: theme.alpha(theme.ink950, 0.80) }
                        }

                        AppIcon {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: 6
                            width: 13
                            height: 13
                            name: filmstripTile.rejected ? "close" : filmstripTile.picked ? "check" : ""
                            color: filmstripTile.rejected ? theme.error : theme.success
                            visible: filmstripTile.picked || filmstripTile.rejected
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 5
                            spacing: 0
                            Repeater {
                                model: Math.min(5, filmstripTile.rating)
                                AppIcon {
                                    width: theme.scaled(14)
                                    height: theme.scaled(14)
                                    name: "star-gold"
                                    color: theme.accentWarm400
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.assetSelected(filmstripTile.index)
                        }
                    }

                    Connections {
                        target: root.catalogAssets
                        function onSelectedIndexChanged() {
                            if (root.catalogAssets.selectedIndex >= 0)
                                filmstrip.positionViewAtIndex(root.catalogAssets.selectedIndex, ListView.Contain)
                        }
                    }
                }

                MiniButton {
                    objectName: "libraryCullNextButton"
                    square: true
                    iconName: "next"
                    role: "icon"
                    tooltip: "Next photo (right arrow)"
                    enabled: root.catalogAssets && root.catalogAssets.selectedIndex >= 0 &&
                             root.catalogAssets.selectedIndex + 1 < root.catalogAssets.assetCount
                    onClicked: root.catalogAssets.selectRelative(1)
                }
            }
        }
    }

    property real _panStartX: 0
    property real _panStartY: 0
}
