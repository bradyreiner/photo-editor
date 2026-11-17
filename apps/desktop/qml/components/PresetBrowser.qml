import QtQuick
import QtCore
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Item {
    id: root

    property var controller: null
    property var presetRows: []
    property var presetNames: []
    property var categoryFilters: ["All"]
    property var packFilters: ["All"]
    property string selectedPresetName: ""
    property string selectedMetaText: "No presets available"
    property string targetSummary: "Current edit target"
    property string selectedTab: "All"
    property string searchText: ""
    property string selectedCategory: "All"
    property string selectedPack: "All"
    property string previewedPresetName: ""
    property int favoriteRevision: 0
    property bool favoritesOnly: false
    property bool hasAsset: false
    property bool canApply: true
    property bool previewRequestsEnabled: true
    property real suspendedPresetListContentY: 0
    property bool presetListScrollRestorePending: false
    property string applyDisabledReason: ""
    property real intensity: 1
    property var changedControls: []

    readonly property bool compactSamples: browserPreferences.compactSamples
    readonly property bool showOriginalSamples: browserPreferences.showOriginalSamples
    readonly property bool amountControlsExpanded: browserPreferences.amountControlsExpanded
    readonly property bool selectedPresetInResults: presetNames.indexOf(selectedPresetName) >= 0

    signal createRequested()
    signal searchEdited(string value)
    signal tabSelected(string value)
    signal favoritesOnlyToggled()
    signal categorySelected(string value)
    signal packSelected(string value)
    signal presetSelected(string name)
    signal presetPreviewRequested(string name)
    signal selectedPreviewRequested(string name)
    signal presetHoverPreviewCleared()
    signal presetPreviewCleared()
    signal favoriteToggled(string name)
    signal quickSelectRequested(var anchor)
    signal applyRequested()
    signal intensityEdited(real value)

    implicitWidth: theme.scaled(240)
    implicitHeight: browserContents.implicitHeight
    Layout.minimumWidth: 0
    Layout.preferredWidth: theme.scaled(300)

    Components.Theme { id: theme }
    Settings {
        id: browserPreferences
        category: "StudioPresetBrowser"
        property bool compactSamples: false
        property bool showOriginalSamples: false
        property bool amountControlsExpanded: false
    }

    function exampleThumbnailSource(name) {
        if (!controller || typeof controller.presetExampleThumbnailSource !== "function")
            return ""
        try {
            return controller.presetExampleThumbnailSource(name)
        } catch (error) {
            return ""
        }
    }

    function originalThumbnailSource() {
        if (!controller || typeof controller.exampleThumbnailOriginalSource !== "function")
            return ""
        try {
            return controller.exampleThumbnailOriginalSource()
        } catch (error) {
            return ""
        }
    }

    function restoreSuspendedPresetListScrollPosition() {
        if (!root.previewRequestsEnabled || !root.presetListScrollRestorePending)
            return
        if (presetList.count <= 0) {
            if (root.presetRows.length > 0)
                return
            root.suspendedPresetListContentY = 0
            root.presetListScrollRestorePending = false
            return
        }
        if (presetList.contentHeight <= 0)
            return
        const maximumContentY = Math.max(0, presetList.contentHeight - presetList.height)
        const restoredContentY = Math.max(0, Math.min(root.suspendedPresetListContentY, maximumContentY))
        presetList.contentY = restoredContentY
        root.suspendedPresetListContentY = restoredContentY
        root.presetListScrollRestorePending = false
    }

    onPreviewRequestsEnabledChanged: {
        root.presetListScrollRestorePending = true
        if (previewRequestsEnabled)
            Qt.callLater(root.restoreSuspendedPresetListScrollPosition)
        else
            root.presetHoverPreviewCleared()
    }

    function isFavorite(name) {
        if (!controller || typeof controller.isPresetFavorite !== "function")
            return false
        return controller.isPresetFavorite(name)
    }

    function presetDescription(name) {
        if (!controller || String(name || "").length === 0)
            return ""
        const kind = controller.isUserPreset(name) ? "Custom" : "Looksy"
        const category = String(controller.presetCategoryName(name) || "")
        return category.length > 0 ? kind + " · " + category : kind + " look"
    }

    ColumnLayout {
        id: browserContents
        objectName: "presetBrowserContents"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: theme.space8

        Item {
            id: browserHeadingRow
            Layout.fillWidth: true
            readonly property bool stackActions: root.width < 300 || theme.interfaceScale >= 1.5
            implicitHeight: stackActions
                            ? browserHeading.implicitHeight + theme.space6 + createPresetButton.implicitHeight
                            : Math.max(browserHeading.implicitHeight, createPresetButton.implicitHeight)
            Label {
                id: browserHeading
                objectName: "studioPresetBrowserHeading"
                x: 0
                y: 0
                width: browserHeadingRow.stackActions
                       ? browserHeadingRow.width
                       : Math.max(0, browserHeadingRow.width - createPresetButton.implicitWidth - theme.space8)
                text: root.width < 280 ? "Looks" : "Find a look"
                color: theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeTitle
                font.weight: Font.DemiBold
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
            Components.MiniButton {
                id: createPresetButton
                objectName: "studioCreatePresetButton"
                x: browserHeadingRow.stackActions
                   ? 0 : browserHeadingRow.width - width
                y: browserHeadingRow.stackActions
                   ? browserHeading.implicitHeight + theme.space6
                   : (browserHeadingRow.height - height) / 2
                width: browserHeadingRow.stackActions ? browserHeadingRow.width : implicitWidth
                text: root.width < 280 ? "New" : "Create"
                role: "primary"
                iconName: "plus"
                showLeadingIcon: true
                Accessible.name: "Create preset"
                enabled: root.hasAsset
                tooltip: "Save your current adjustments as a reusable preset."
                onClicked: root.createRequested()
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.width >= 300 && theme.interfaceScale < 1.5
            text: "Preview a sample first. Apply commits changes to the selected target."
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
            maximumLineCount: 2
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
        }

        Rectangle {
            objectName: "studioPresetSelectedDetail"
            Layout.fillWidth: true
            implicitHeight: selectedSummary.implicitHeight + theme.space16
            radius: theme.radius4
            color: theme.alpha(theme.ink950, 0.28)
            border.color: theme.alpha(theme.polar, 0.12)
            ColumnLayout {
                id: selectedSummary
                anchors.fill: parent
                anchors.margins: theme.space8
                spacing: theme.space6
                Item {
                    id: selectedPresetHeader
                    Layout.fillWidth: true
                    readonly property bool stackActions: root.width < 300 || theme.interfaceScale >= 1.5
                    implicitHeight: stackActions
                                    ? selectedPresetSummary.implicitHeight + theme.space6 + studioPresetQuickSelectButton.implicitHeight
                                    : Math.max(selectedPresetSummary.implicitHeight, studioPresetQuickSelectButton.implicitHeight)
                    ColumnLayout {
                        id: selectedPresetSummary
                        x: 0
                        y: 0
                        width: selectedPresetHeader.stackActions
                               ? selectedPresetHeader.width
                               : Math.max(0, selectedPresetHeader.width - studioPresetQuickSelectButton.implicitWidth - theme.space6)
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: theme.space2
                        Label {
                            objectName: "studioPresetSelectedName"
                            Layout.fillWidth: true
                            text: root.selectedPresetName.length > 0 ? root.selectedPresetName : "Choose a look"
                            color: theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                            maximumLineCount: 2
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideRight
                        }
                        Label {
                            objectName: "studioPresetSelectedMeta"
                            Layout.fillWidth: true
                            text: root.selectedMetaText + (root.selectedPresetInResults || root.selectedPresetName.length === 0
                                                           ? "" : " · outside current filters")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            maximumLineCount: 2
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideRight
                        }
                        Label {
                            objectName: "studioPresetSelectedTarget"
                            Layout.fillWidth: true
                            text: "Target · " + root.targetSummary
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            maximumLineCount: 2
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideRight
                        }
                    }
                    Components.MiniButton {
                        id: studioPresetQuickSelectButton
                        objectName: "studioPresetQuickSelect"
                        x: selectedPresetHeader.stackActions
                           ? 0 : selectedPresetHeader.width - width
                        y: selectedPresetHeader.stackActions
                           ? selectedPresetSummary.implicitHeight + theme.space6
                           : (selectedPresetHeader.height - height) / 2
                        width: selectedPresetHeader.stackActions ? selectedPresetHeader.width : implicitWidth
                        text: root.width < 280 ? "Choose" : "Browse"
                        role: "secondary"
                        Accessible.name: "Browse presets"
                        enabled: root.controller && root.controller.presetNames.length > 0
                        tooltip: "Choose a preset by name."
                        onClicked: root.quickSelectRequested(studioPresetQuickSelectButton)
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: theme.interfaceScale < 1.5
                    text: root.width < 300
                          ? "Preview only · Apply commits."
                          : "Preview is temporary · Apply commits to the selected target."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                    maximumLineCount: 2
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                Label {
                    objectName: "studioPresetTargetRestriction"
                    Layout.fillWidth: true
                    visible: root.selectedPresetName.length > 0 && !root.canApply
                    text: root.applyDisabledReason
                    color: theme.warning
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                    maximumLineCount: 2
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideRight
                    Accessible.name: "Preset target unavailable"
                    Accessible.description: root.applyDisabledReason
                }
                Flow {
                    id: presetActionFlow
                    Layout.fillWidth: true
                    spacing: theme.space6
                    Components.MiniButton {
                        objectName: "studioPresetPreviewSelectedButton"
                        width: root.width < 280 ? presetActionFlow.width
                                                : (presetActionFlow.width - presetActionFlow.spacing) / 2
                        text: root.previewedPresetName === root.selectedPresetName && root.selectedPresetName.length > 0 ? "Clear preview" : "Preview"
                        role: "secondary"
                        enabled: root.selectedPresetName.length > 0 && root.hasAsset && root.canApply
                        onClicked: {
                            if (root.previewedPresetName === root.selectedPresetName)
                                root.presetPreviewCleared()
                            else
                                root.selectedPreviewRequested(root.selectedPresetName)
                        }
                    }
                    Components.MiniButton {
                        objectName: "studioApplyPresetButton"
                        width: root.width < 280 ? presetActionFlow.width
                                                : (presetActionFlow.width - presetActionFlow.spacing) / 2
                        text: "Apply"
                        role: "primary"
                        enabled: root.selectedPresetName.length > 0 && root.hasAsset && root.canApply
                        tooltip: "Apply this look to the selected edit target."
                        onClicked: root.applyRequested()
                    }
                }
                Components.MiniButton {
                    objectName: "studioPresetAdjustAmountDisclosure"
                    Layout.fillWidth: true
                    text: (root.width < 300 || theme.interfaceScale >= 1.5
                           ? Math.round(root.intensity * 100).toString() + "%"
                           : "Adjust amount · " + Math.round(root.intensity * 100).toString() + "%")
                    iconName: "adjust"
                    showLeadingIcon: true
                    role: "secondary"
                    active: root.amountControlsExpanded
                    tooltip: "Show the intensity slider and adjustment summary."
                    Accessible.name: "Adjust amount: " + Math.round(root.intensity * 100).toString() + " percent"
                    onClicked: browserPreferences.amountControlsExpanded = !browserPreferences.amountControlsExpanded
                }
                ColumnLayout {
                    objectName: "studioPresetAdjustAmountControls"
                    Layout.fillWidth: true
                    visible: root.amountControlsExpanded
                    spacing: theme.space4
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.space4
                        Label {
                            Layout.fillWidth: true
                            text: "Intensity"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                        }
                        Label {
                            text: Math.round(root.intensity * 100).toString() + "%"
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                        }
                    }
                    Slider {
                        id: presetIntensitySlider
                        objectName: "studioPresetIntensitySlider"
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        stepSize: 0.01
                        value: root.intensity
                        Accessible.name: "Preset intensity"
                        onMoved: root.intensityEdited(value)
                    }
                    Flow {
                        objectName: "studioPresetChangedControls"
                        Layout.fillWidth: true
                        spacing: theme.space4
                        visible: root.selectedPresetName.length > 0 && root.changedControls.length > 0
                        Repeater {
                            model: root.changedControls.slice(0, 6)
                            Components.FilterChip {
                                required property string modelData
                                text: modelData
                                active: true
                                enabled: false
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            objectName: "studioPresetLibraryTabs"
            Layout.fillWidth: true
            spacing: theme.space4
            Repeater {
                model: [
                    { label: "All", value: "All" },
                    { label: theme.interfaceScale > 1 ? "Favs" : "Favorites", value: "Favorites" },
                    { label: theme.interfaceScale > 1 ? "Mine" : "My Presets", value: "My Presets" }
                ]
                Components.MiniButton {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: modelData.label
                    Accessible.name: modelData.value
                    role: "segmented"
                    active: root.selectedTab === modelData.value
                    onClicked: root.tabSelected(modelData.value)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space6
            Components.ToneTextField {
                objectName: "studioPresetSearchField"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                placeholderText: "Search presets"
                text: root.searchText
                onTextEdited: root.searchEdited(text)
            }
            Components.MiniButton {
                objectName: "studioPresetFavoritesFilterButton"
                square: true
                iconName: root.favoritesOnly ? "rating" : "star"
                showLeadingIcon: true
                role: "icon"
                active: root.favoritesOnly
                tooltip: root.favoritesOnly ? "Clear the favorites-only filter." : "Show only favorited presets."
                Accessible.name: root.favoritesOnly ? "Clear favorites-only filter" : "Show only favorites"
                onClicked: root.favoritesOnlyToggled()
            }
        }

        RowLayout {
            objectName: "studioPresetCategoryFilterRow"
            Layout.fillWidth: true
            spacing: theme.space6
            Components.ToneComboBox {
                objectName: "studioPresetCategoryFilterCombo"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: root.categoryFilters.map(function(value) {
                    if (value !== "All") return value
                    return root.width < 280 ? "Category" : "All categories"
                })
                currentIndex: Math.max(0, root.categoryFilters.indexOf(root.selectedCategory))
                Accessible.name: root.selectedCategory === "All" ? "All categories" : "Category: " + root.selectedCategory
                onActivated: function(index) { root.categorySelected(root.categoryFilters[index]) }
            }
            Components.ToneComboBox {
                objectName: "studioPresetPackFilterCombo"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: root.packFilters.map(function(value) {
                    if (value !== "All") return value
                    return root.width < 280 ? "Pack" : "All packs"
                })
                currentIndex: Math.max(0, root.packFilters.indexOf(root.selectedPack))
                Accessible.name: root.selectedPack === "All" ? "All packs" : "Pack: " + root.selectedPack
                onActivated: function(index) { root.packSelected(root.packFilters[index]) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space4
            Label {
                objectName: "studioPresetResultCount"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.presetNames.length + (root.presetNames.length === 1 ? " look" : " looks")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
            Components.MiniButton {
                objectName: "studioPresetThumbnailSizeButton"
                text: root.width < 280
                      ? (root.compactSamples ? "Small" : "Large")
                      : (root.compactSamples ? "Samples · Small" : "Samples · Large")
                role: "secondary"
                tooltip: root.compactSamples ? "Use larger sample previews." : "Use smaller sample previews."
                onClicked: browserPreferences.compactSamples = !browserPreferences.compactSamples
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space4
            Label {
                Layout.fillWidth: true
                text: "Example view"
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
            Components.MiniButton {
                objectName: "studioPresetOriginalSampleButton"
                text: root.width < 280 ? "Original" : "Original sample"
                role: "secondary"
                active: root.showOriginalSamples
                tooltip: "Show the unedited reference sample in each card."
                onClicked: browserPreferences.showOriginalSamples = !browserPreferences.showOriginalSamples
            }
        }

        ListView {
            id: presetList
            objectName: "studioPresetLibraryList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(theme.scaled(260), Math.min(theme.scaled(430), contentHeight))
            visible: root.presetRows.length > 0
            clip: true
            spacing: theme.space6
            model: root.previewRequestsEnabled ? root.presetRows : []
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            onContentYChanged: {
                if (root.previewRequestsEnabled && !root.presetListScrollRestorePending)
                    root.suspendedPresetListContentY = contentY
            }
            onContentHeightChanged: {
                if (root.presetListScrollRestorePending)
                    Qt.callLater(root.restoreSuspendedPresetListScrollPosition)
            }
            onCountChanged: {
                if (root.presetListScrollRestorePending)
                    Qt.callLater(root.restoreSuspendedPresetListScrollPosition)
            }

            delegate: Item {
                id: presetRow
                required property var modelData
                readonly property bool packHeader: modelData.kind === "pack"
                readonly property string presetName: packHeader ? "" : String(modelData.name || "")
                objectName: packHeader ? "studioPresetPackHeader" : "studioPresetLibraryCard"
                width: ListView.view ? ListView.view.width : 0
                height: packHeader ? theme.scaled(24) : (exampleCard.implicitHeight > 0 ? exampleCard.implicitHeight : theme.scaled(root.compactSamples ? 180 : 224))

                RowLayout {
                    anchors.fill: parent
                    visible: presetRow.packHeader
                    spacing: theme.space6
                    Label {
                        Layout.fillWidth: true
                        text: String(presetRow.modelData.label || "")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                        font.letterSpacing: 0.4
                        elide: Text.ElideRight
                    }
                    Label {
                        text: String(presetRow.modelData.count || 0)
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                    }
                }

                Components.VisualExampleCard {
                    id: exampleCard
                    anchors.fill: parent
                    visible: !presetRow.packHeader
                    title: presetRow.presetName
                    description: root.presetDescription(presetRow.presetName)
                    thumbnailSource: root.previewRequestsEnabled && !presetRow.packHeader
                        ? root.exampleThumbnailSource(presetRow.presetName) : ""
                    originalSource: root.previewRequestsEnabled && !presetRow.packHeader
                        ? root.originalThumbnailSource() : ""
                    showOriginal: root.showOriginalSamples
                    selected: root.selectedPresetName === presetRow.presetName
                    favorite: root.favoriteRevision >= 0 && root.isFavorite(presetRow.presetName)
                    favoriteAvailable: true
                    favoriteButtonObjectName: "studioPresetFavoriteButton"
                    previewActive: true
                    badgeText: "Sample"
                    compact: root.compactSamples
                    objectName: "studioPresetExampleCard"
                    onHoveredChanged: {
                        if (hovered && root.canApply)
                            root.presetPreviewRequested(presetRow.presetName)
                        else if (!hovered)
                            root.presetHoverPreviewCleared()
                    }
                    onClicked: root.presetSelected(presetRow.presetName)
                    onFavoriteToggled: root.favoriteToggled(presetRow.presetName)
                }
            }
        }

        Rectangle {
            objectName: "studioPresetEmptyState"
            Layout.fillWidth: true
            Layout.preferredHeight: root.selectedTab === "My Presets" && root.presetNames.length === 0 ? theme.scaled(102) : theme.scaled(82)
            visible: root.presetNames.length === 0
            radius: theme.radius4
            color: theme.alpha(theme.ink950, 0.22)
            border.color: theme.alpha(theme.polar, 0.12)
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.space12
                spacing: theme.space4
                Label {
                    Layout.fillWidth: true
                    text: root.selectedTab === "My Presets" ? "Your custom library starts here" : "No looks match these filters"
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    text: root.hasAsset ? "Clear a filter to browse, or save your current look as a preset." : "Open a photo to preview and apply a look."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                    wrapMode: Text.WordWrap
                }
                Components.MiniButton {
                    objectName: "studioPresetEmptyCreateButton"
                    visible: root.hasAsset && root.selectedTab === "My Presets"
                    text: "Create your first preset"
                    role: "secondary"
                    onClicked: root.createRequested()
                }
            }
        }


    }
}
