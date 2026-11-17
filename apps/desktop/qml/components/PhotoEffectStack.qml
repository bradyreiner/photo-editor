pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import "."

ColumnLayout {
    id: root
    Layout.minimumWidth: 0
    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    property string expandedEffectId: ""
    property int effectsRevision: 0
    property bool compactCards: preferences.compactCards
    property bool headerVisible: true
    property bool addButtonVisible: true
    property bool timelineStyle: false
    property bool effectAmountEditActive: false
    property var maskEditHandler: null
    signal effectMaskEditRequested(int targetLayerIndex, string maskSetId)
    function editEffectMask(effectId) {
        if (effectPicker.visible && effectPicker.previewId === String(effectId) &&
                !root.controller.finishPhotoLayerEffectPreview(true)) return
        const effect = root.effects.find(function(item) { return String(item.id) === String(effectId) })
        const setId = String(effect ? effect.maskId || "" : "")
        if (!setId.length) return
        const selected = typeof root.maskEditHandler === "function"
            ? root.maskEditHandler(root.layerIndex, setId)
            : root.controller.selectPhotoLayerEffectMask(root.layerIndex, effectId)
        if (!selected) return
        root.effectMaskEditRequested(root.layerIndex, setId)
        effectPicker.close()
    }
    onLayerIndexChanged: {
        root.finishEffectAmountEdit()
        expandedEffectId = ""
        effectPicker.close()
    }
    onLockedChanged: {
        if (locked) {
            root.finishEffectAmountEdit()
            effectPicker.close()
        }
    }
    Component.onDestruction: {
        root.finishEffectAmountEdit()
        root.controller.finishPhotoLayerEffectPreview(false)
    }
    Connections {
        target: root.controller
        function onAssetChanged() { effectPicker.close() }
    }
    readonly property var effects: {
        const revision = root.layerData
        const refreshToken = root.effectsRevision
        return revision && root.layerIndex >= 0 && refreshToken >= 0
            ? root.controller.photoLayerEffects(root.layerIndex) : []
    }
    onEffectsChanged: Qt.callLater(root.syncEffectRows)
    onEffectsRevisionChanged: Qt.callLater(root.syncEffectRows)
    function syncEffectRows() {
        const rows = root.effects
        let sameIds = effectRowsModel.count === rows.length
        if (sameIds) {
            for (let i = 0; i < rows.length; ++i) {
                if (String(effectRowsModel.get(i).effectData.id) !== String(rows[i].id)) {
                    sameIds = false
                    break
                }
            }
        }
        if (sameIds) {
            for (let i = 0; i < rows.length; ++i)
                effectRowsModel.setProperty(i, "effectData", rows[i])
            return
        }
        effectRowsModel.clear()
        for (let i = 0; i < rows.length; ++i)
            effectRowsModel.append({effectData: rows[i]})
    }
    function refreshEffects() {
        effectsRevision += 1
        Qt.callLater(root.syncEffectRows)
    }
    function beginEffectAmountEdit() {
        if (effectAmountEditActive) return
        effectAmountEditActive = true
        root.controller.beginCoalescedEdit()
    }
    function finishEffectAmountEdit() {
        if (!effectAmountEditActive) return
        effectAmountEditActive = false
        root.controller.endCoalescedEdit()
    }
    function editSpatialEffect(type, id, values, amount, enabled) {
        if (type === "motion_blur")
            return controller.setPhotoLayerMotionBlur(layerIndex, id, values.distance, values.angle, amount, enabled)
        if (type === "dragged_shutter")
            return controller.setPhotoLayerDraggedShutter(layerIndex, id, values.distance, values.angle,
                values.falloff, values.highlightWeighting, values.protectSubject, amount, enabled)
        if (type === "optical_diffusion")
            return controller.setPhotoLayerOpticalDiffusion(layerIndex, id, values.diffusionRadius,
                values.highlightThreshold, values.highlightKnee, values.diffusionStrength,
                values.warmth, values.highlightProtection, amount, enabled)
        if (type === "imperfect_print")
            return controller.setPhotoLayerImperfectPrint(layerIndex, id, values.paperScale,
                values.paperTexture, values.inkSpread, values.inkDensity, values.redRegistrationX,
                values.redRegistrationY, values.blueRegistrationX, values.blueRegistrationY,
                values.seed, amount, enabled)
        if (type === "film_grain" || type === "light_leak" || type === "split_tone" ||
                type === "vignette" || type === "duotone" || type === "soft_fade")
            return controller.setPhotoLayerEffectParameters(layerIndex, type, id, values, amount, enabled)
        return controller.setPhotoLayerUnsharpMask(layerIndex, id, values.sharpenRadius,
            values.strength, values.threshold, amount, enabled)
    }
    function editExtendedEffect(type, id, values, amount, enabled) {
        return controller.setPhotoLayerEffectParameters(layerIndex, type, id, values, amount, enabled)
    }
    function refreshEffectPreviewParameters() {
        const rows = controller.photoLayerEffects(layerIndex)
        for (let i = 0; i < rows.length; ++i) {
            if (String(rows[i].id) === effectPicker.previewId) {
                effectPicker.previewParameters = rows[i]
                effectPicker.previewExtended = Boolean(rows[i].extended)
                return true
            }
        }
        return false
    }
    Theme { id: theme }
    Settings {
        id: preferences
        objectName: "photoEffectPreferences"
        category: "PhotoEffectStack"
        property bool compactCards: true
        property bool expandedGallery: false
    }
    ListModel {
        id: effectRowsModel
        dynamicRoles: true
    }
    Component.onCompleted: root.syncEffectRows()
    spacing: root.compactCards ? theme.space4 : theme.space8

    Flow {
        objectName: "photoEffectHeader"
        visible: root.headerVisible
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.maximumWidth: root.width
        spacing: theme.space4
        RowLayout {
            width: implicitWidth
            height: implicitHeight
            spacing: theme.space4
            AppIcon {
                Layout.preferredWidth: theme.scaled(18)
                Layout.preferredHeight: theme.scaled(18)
                name: "effects"
                color: theme.textStrong
            }
            Label {
                text: qsTr("Effects")
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeSection
                font.weight: theme.weightSection
            }
        }
        MiniButton {
            objectName: "photoEffectDensityToggle"
            width: implicitWidth
            height: implicitHeight
            text: root.compactCards ? qsTr("Compact") : qsTr("Comfortable")
            iconName: "density"
            showLeadingIcon: true
            role: "segmented"
            active: !root.compactCards
            Accessible.name: root.compactCards ? qsTr("Use comfortable effect cards") : qsTr("Use compact effect cards")
            tooltip: qsTr("Choose tighter or roomier spacing for the effect stack.")
            onClicked: preferences.compactCards = !preferences.compactCards
        }
    }

    ActionButton {
        id: addEffectButton
        objectName: "photoAddEffectButton"
        visible: root.addButtonVisible
        text: qsTr("Add effect")
        iconName: "effects"
        showLeadingIcon: true
        tone: "primary"
        enabled: !root.locked
        Layout.fillWidth: true
        onClicked: effectPicker.open()
    }
    Label {
        objectName: "photoEffectEmptyState"
        visible: root.effects.length === 0
        Layout.fillWidth: true
        text: qsTr("No effects yet. Add an effect to start shaping this layer.")
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
    Item {
        Layout.preferredWidth: 0
        Layout.minimumWidth: 0
        Layout.maximumWidth: 0
        Layout.preferredHeight: 0
        Layout.minimumHeight: 0
        Layout.maximumHeight: 0
        implicitWidth: 0
        implicitHeight: 0
        AppPopup {
            id: effectPicker
            objectName: "photoEffectPicker"
            parent: Overlay.overlay
            readonly property real availableOverlayWidth: Overlay.overlay ? Overlay.overlay.width : 720
            readonly property real availableOverlayHeight: Overlay.overlay ? Overlay.overlay.height : 720
            readonly property bool wideGallery: effectPicker.browsing && preferences.expandedGallery
            readonly property real compactGalleryWidth: Math.max(theme.scaled(320),
                Math.min(root.width, theme.scaled(420)))
            width: Math.min(popupAvailableWidth, Math.max(1, Math.min(
                wideGallery ? 760 : compactGalleryWidth, Math.max(1, availableOverlayWidth - 24))))
            property point anchorPosition: Qt.point(0, 0)
            x: Math.max(12, Math.min(anchorPosition.x + root.width - width, availableOverlayWidth - width - 12))
            y: Math.max(12, Math.min(anchorPosition.y + addEffectButton.height + 6, availableOverlayHeight - height - 12))
            function resetScroll() {
                if (effectPicker.browsing)
                    effectGalleryList.contentY = effectGalleryList.originY
                else if (effectPickerBodyScrollView.contentItem &&
                        typeof effectPickerBodyScrollView.contentItem.contentY === "number")
                    effectPickerBodyScrollView.contentItem.contentY = 0
            }
            onBrowsingChanged: Qt.callLater(resetScroll)
            onSelectedCategoryChanged: Qt.callLater(resetScroll)
            function updateAnchor() {
                if (parent) anchorPosition = addEffectButton.mapToItem(parent, 0, 0)
            }
            height: Math.min(popupAvailableHeight, Math.max(1, Math.min(implicitHeight, Math.max(176, availableOverlayHeight - 24))))
            padding: theme.popupPadding
            modal: false
            dim: false
            background: PopupSurface {}
            focus: true
            property string selectedCategory: ""
            property bool browsing: true
            property string previewId: ""
            property string previewType: ""
            property string previewName: ""
            property var previewParameters: ({})
            property bool previewExtended: false
            readonly property bool spatialPreview: previewType === "motion_blur" || previewType === "dragged_shutter" || previewType === "unsharp_mask" || previewType === "optical_diffusion" || previewType === "imperfect_print" || previewType === "film_grain" || previewType === "light_leak" || previewType === "split_tone" || previewType === "vignette" || previewType === "duotone" || previewType === "soft_fade"
            property string errorMessage: ""
            property string maskMessage: ""
            function previewNumber(key, fallback) {
                const value = Number(previewParameters[key])
                return Number.isFinite(value) ? value : fallback
            }
            onAboutToHide: root.controller.finishPhotoLayerEffectPreview(false)
            onClosed: {
                previewId = ""
                previewType = ""
                previewName = ""
                previewExtended = false
                errorMessage = ""
                maskMessage = ""
            }
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            onAboutToShow: {
                updateAnchor()
                selectedCategory = ""
                effectSearch.text = ""
                browsing = true
                Qt.callLater(resetScroll)
                Qt.callLater(function() {
                    if (effectPicker.visible && effectPicker.browsing && !effectCategoryPicker.popup.visible)
                        effectSearch.forceActiveFocus()
                })
            }
            onOpened: {
                updateAnchor()
            }
            Connections {
                target: effectPicker.parent
                function onWidthChanged() { effectPicker.updateAnchor() }
                function onHeightChanged() { effectPicker.updateAnchor() }
            }
            function groupsFor(rows) {
                const groups = ({})
                rows.forEach(function(row) {
                    const category = String(row.category || qsTr("Other"))
                    if (!groups[category]) groups[category] = []
                    groups[category].push(row)
                })
                return Object.keys(groups).sort(function(a, b) { return a.localeCompare(b) }).map(function(category) {
                    return {category: category, rows: groups[category].sort(function(a, b) {
                        return String(a.name).localeCompare(String(b.name))
                    })}
                })
            }
            readonly property var categories: groupsFor(root.controller.availablePhotoEffects().concat(
                typeof root.controller.availablePhotoEffectLooks === "function" ? root.controller.availablePhotoEffectLooks() : []))
                .map(function(group) { return group.category })
            readonly property var effectGroups: groupsFor(choices)
            readonly property var lookGroups: groupsFor(lookChoices)
            readonly property var gallerySections: {
                const sections = []
                effectGroups.forEach(function(group) {
                    sections.push({kind: "effects", category: group.category, rows: group.rows})
                })
                if (lookGroups.length > 0) {
                    sections.push({kind: "looks-intro", category: "", rows: []})
                    lookGroups.forEach(function(group) {
                        sections.push({kind: "looks", category: group.category, rows: group.rows})
                    })
                }
                return sections
            }
            readonly property var choices: root.controller.availablePhotoEffects().filter(function(effect) {
                return (!effectPicker.selectedCategory || String(effect.category) === effectPicker.selectedCategory) &&
                    (String(effect.name) + " " + String(effect.category) + " " + String(effect.description)).toLowerCase()
                    .indexOf(effectSearch.text.trim().toLowerCase()) >= 0
            })
            readonly property var lookChoices: typeof root.controller.availablePhotoEffectLooks === "function"
                ? root.controller.availablePhotoEffectLooks().filter(function(look) {
                    return (!effectPicker.selectedCategory || String(look.category) === effectPicker.selectedCategory) &&
                        (String(look.name) + " " + String(look.category) + " " + String(look.description)).toLowerCase()
                        .indexOf(effectSearch.text.trim().toLowerCase()) >= 0
                }) : []
            contentItem: ColumnLayout {
                spacing: theme.space8
                Label {
                    objectName: "photoEffectPreviewTargetLabel"
                    Layout.fillWidth: true
                    text: qsTr("Filters · %1").arg(String(root.layerData &&
                        (root.layerData.displayName || root.layerData.name) || qsTr("Selected layer")))
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeSection
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                ToneTextField {
                    id: effectSearch
                    objectName: "photoEffectSearchField"
                    visible: effectPicker.browsing
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search filters and looks")
                    Accessible.name: qsTr("Search filters and looks")
                    selectByMouse: true
                    onTextChanged: Qt.callLater(effectPicker.resetScroll)
                }
                RowLayout {
                    visible: effectPicker.browsing
                    Layout.fillWidth: true
                    Label {
                        visible: root.width >= theme.scaled(360)
                        text: qsTr("Group")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                    }
                    ToneComboBox {
                        id: effectCategoryPicker
                        objectName: "photoEffectCategoryPicker"
                        Layout.fillWidth: true
                        accessibleLabel: qsTr("Filter group")
                        model: [qsTr("All groups")].concat(effectPicker.categories)
                        currentIndex: effectPicker.selectedCategory.length === 0 ? 0
                            : effectPicker.categories.indexOf(effectPicker.selectedCategory) + 1
                        onActivated: function(index) {
                            effectPicker.selectedCategory = index === 0 ? "" : effectPicker.categories[index - 1]
                        }
                    }
                    MiniButton {
                        objectName: "photoEffectCategoryReset"
                        visible: effectPicker.selectedCategory.length > 0
                        text: qsTr("All")
                        Accessible.name: qsTr("Show all filter groups")
                        onClicked: effectPicker.selectedCategory = ""
                    }
                }
                MiniButton {
                    objectName: "photoEffectGallerySizeToggle"
                    visible: effectPicker.browsing
                    Layout.fillWidth: true
                    text: root.width < theme.scaled(280)
                        ? (preferences.expandedGallery ? qsTr("Compact") : qsTr("Expand"))
                        : (preferences.expandedGallery ? qsTr("Compact gallery") : qsTr("Expand gallery"))
                    iconName: preferences.expandedGallery ? "panel-left" : "maximize"
                    showLeadingIcon: true
                    Accessible.name: preferences.expandedGallery
                        ? qsTr("Compact gallery") : qsTr("Expand gallery")
                    tooltip: preferences.expandedGallery
                        ? qsTr("Return to the compact picker and keep more of the photo in view.")
                        : qsTr("Show larger example cards in a wider two-column gallery.")
                    onClicked: preferences.expandedGallery = !preferences.expandedGallery
                }
                RowLayout {
                    visible: effectPicker.previewId.length > 0 && !effectPicker.browsing
                    Layout.fillWidth: true
                    Label {
                        objectName: "photoEffectPreviewName"
                        visible: !effectPicker.browsing
                        Layout.fillWidth: true
                        text: qsTr("Preview: %1").arg(effectPicker.previewName)
                        color: theme.textStrong
                        wrapMode: Text.WordWrap
                    }
                    MiniButton {
                        objectName: "photoEffectChangeFilterButton"
                        text: effectPicker.browsing ? qsTr("Back to preview") : qsTr("Change filter")
                        iconName: effectPicker.browsing ? "edit" : "filter"
                        showLeadingIcon: true
                        onClicked: effectPicker.browsing = !effectPicker.browsing
                    }
                }
                Label {
                    objectName: "photoEffectTransactionHint"
                    visible: effectPicker.browsing || effectPicker.previewId.length > 0
                    Layout.fillWidth: true
                    text: effectPicker.browsing
                        ? qsTr("Filters open in a live preview. Looks apply immediately.")
                        : qsTr("Live preview · Apply commits this effect; Cancel restores the previous stack.")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                ScrollView {
                    id: effectPickerBodyScrollView
                    objectName: effectPicker.browsing ? "photoEffectPickerBodyScrollView" : "photoEffectSettingsScrollView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    Layout.preferredHeight: 470
                    implicitHeight: 470
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: effectPickerBody.implicitHeight
                    ColumnLayout {
                        id: effectPickerBody
                        width: effectPickerBodyScrollView.availableWidth
                        spacing: theme.space8
                        ColumnLayout {
                            visible: effectPicker.browsing
                            Layout.fillWidth: true
                            spacing: theme.space8
                            ListView {
                                id: effectGalleryList
                                objectName: "photoEffectGalleryList"
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(contentHeight, effectPickerBodyScrollView.availableHeight)
                                implicitHeight: Math.min(contentHeight, effectPickerBodyScrollView.availableHeight)
                                visible: effectPicker.browsing && effectPicker.gallerySections.length > 0
                                clip: true
                                spacing: theme.space12
                                cacheBuffer: 0
                                model: effectPicker.gallerySections
                                ScrollBar.vertical: ScrollBar {
                                    objectName: "photoEffectGalleryScrollBar"
                                    policy: ScrollBar.AsNeeded
                                }
                                WheelHandler {
                                    target: null
                                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                    onWheel: function(event) {
                                        const delta = event.pixelDelta.y !== 0
                                            ? event.pixelDelta.y : event.angleDelta.y / 120 * 48
                                        if (delta === 0) return
                                        const minimumY = effectGalleryList.originY
                                        const maximumY = minimumY + Math.max(0,
                                            effectGalleryList.contentHeight - effectGalleryList.height)
                                        effectGalleryList.contentY = Math.max(minimumY,
                                            Math.min(maximumY, effectGalleryList.contentY - delta))
                                        event.accepted = true
                                    }
                                }
                                delegate: Item {
                                    id: gallerySection
                                    required property var modelData
                                    readonly property bool isLookSection: modelData.kind === "looks"
                                    objectName: modelData.kind === "effects" ? "photoEffectGroup_" + String(modelData.category)
                                        : modelData.kind === "looks" ? "photoEffectLookGroup_" + String(modelData.category)
                                        : "photoEffectLooksIntroduction"
                                    width: ListView.view.width
                                    height: sectionContent.implicitHeight
                                    ColumnLayout {
                                        id: sectionContent
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        spacing: theme.space6
                                        Label {
                                            visible: gallerySection.modelData.kind === "looks-intro"
                                            Layout.fillWidth: true
                                            text: qsTr("Looks")
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeSection
                                            font.weight: Font.DemiBold
                                        }
                                        Label {
                                            visible: gallerySection.modelData.kind === "looks-intro"
                                            Layout.fillWidth: true
                                            text: qsTr("One click adds an editable stack of effects immediately.")
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                        }
                                        Label {
                                            visible: gallerySection.modelData.kind !== "looks-intro"
                                            objectName: gallerySection.isLookSection
                                                ? "photoEffectLookGroupLabel_" + String(gallerySection.modelData.category)
                                                : "photoEffectGroupLabel_" + String(gallerySection.modelData.category)
                                            Layout.fillWidth: true
                                            text: String(gallerySection.modelData.category)
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeLabel
                                            font.weight: Font.DemiBold
                                        }
                                        GridLayout {
                                            visible: gallerySection.modelData.kind !== "looks-intro"
                                            Layout.fillWidth: true
                                            columns: width >= theme.scaled(600) ? 2 : 1
                                            rowSpacing: theme.space8
                                            columnSpacing: theme.space8
                                            Repeater {
                                                model: gallerySection.modelData.rows
                                                delegate: VisualExampleCard {
                                                    id: exampleCard
                                                    required property var modelData
                                                    readonly property bool mapExampleUnavailable: !gallerySection.isLookSection &&
                                                        String(modelData.type) === "displacement_map"
                                                    objectName: gallerySection.isLookSection
                                                        ? "photoEffectLookChoice_" + String(modelData.id)
                                                        : "photoEffectChoice_" + String(modelData.type)
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 0
                                                    title: String(modelData.name)
                                                    description: String(modelData.description || "") + (mapExampleUnavailable
                                                        ? qsTr(" Example preview requires importing a displacement map.") : "")
                                                    badgeText: gallerySection.isLookSection ? qsTr("APPLIES NOW")
                                                        : mapExampleUnavailable ? qsTr("MAP REQUIRED") : qsTr("LIVE PREVIEW")
                                                    selected: !gallerySection.isLookSection && effectPicker.previewType === String(modelData.type)
                                                    previewActive: true
                                                    compact: root.compactCards
                                                    favoriteAvailable: false
                                                    thumbnailSource: typeof root.controller.effectExampleThumbnailSource === "function"
                                                        ? root.controller.effectExampleThumbnailSource(
                                                            gallerySection.isLookSection ? String(modelData.id) : String(modelData.type),
                                                            gallerySection.isLookSection) : ""
                                                    originalSource: typeof root.controller.exampleThumbnailOriginalSource === "function"
                                                        ? root.controller.exampleThumbnailOriginalSource() : ""
                                                    enabled: gallerySection.isLookSection
                                                        ? (!root.locked && typeof root.controller.applyPhotoLayerEffectLook === "function")
                                                        : !root.locked
                                                    onClicked: {
                                                        if (gallerySection.isLookSection) {
                                                            if (root.controller.applyPhotoLayerEffectLook(root.layerIndex, String(modelData.id)))
                                                                effectPicker.close()
                                                            else
                                                                effectPicker.errorMessage = qsTr("This look could not be added to the selected layer.")
                                                            return
                                                        }
                                                        const added = root.controller.previewPhotoLayerEffect(root.layerIndex, String(modelData.type))
                                                        if (added) {
                                                            const rows = root.controller.photoLayerEffects(root.layerIndex)
                                                            effectPicker.previewId = String(rows[rows.length - 1].id)
                                                            effectPicker.previewType = String(modelData.type)
                                                            effectPicker.previewName = String(modelData.name)
                                                            effectPicker.previewParameters = rows[rows.length - 1]
                                                            effectPicker.previewExtended = Boolean(rows[rows.length - 1].extended)
                                                            effectPicker.errorMessage = ""
                                                            effectPicker.maskMessage = ""
                                                            effectPicker.browsing = false
                                                        } else {
                                                            effectPicker.previewId = ""
                                                            effectPicker.errorMessage = qsTr("Unable to preview this effect. Finish any other edit and try again.")
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Label {
                                visible: effectPicker.browsing && effectPicker.choices.length === 0 && effectPicker.lookChoices.length === 0
                                text: qsTr("No matching filters or looks")
                                color: theme.textMuted
                                Layout.fillWidth: true
                            }
                        }
                        ColumnLayout {
                            id: effectSettings
                            objectName: "photoEffectSettings"
                            visible: effectPicker.previewId.length > 0 && !effectPicker.browsing
                            Layout.fillWidth: true
                            spacing: theme.space8
                            PhotoSpatialEffectControls {
                                visible: effectPicker.previewId.length > 0 && (effectPicker.spatialPreview || effectPicker.previewExtended)
                                Layout.fillWidth: true
                                effectType: effectPicker.previewType
                                parameters: effectPicker.previewParameters
                                controller: root.controller
                                layerIndex: root.layerIndex
                                effectId: effectPicker.previewId
                                extended: effectPicker.previewExtended
                                onParametersEdited: function(values) {
                                    const changed = extended
                                        ? root.editExtendedEffect(effectType, effectId, values, previewAmount.value / 100, true)
                                        : root.editSpatialEffect(effectType, effectId, values, previewAmount.value / 100, true)
                                    if (changed) {
                                        if (extended) root.refreshEffectPreviewParameters()
                                        else effectPicker.previewParameters = values
                                    }
                                }
                                onRefreshRequested: root.refreshEffectPreviewParameters()
                            }
                            InspectorSlider {
                                id: previewSize
                                objectName: "photoEffectPreviewSize"
                                visible: effectPicker.previewId.length > 0 && effectPicker.previewType !== "invert_linear_rgb" && !effectPicker.spatialPreview && !effectPicker.previewExtended
                                Layout.fillWidth: true
                                label: effectPicker.previewType === "posterize_linear_rgb" ? qsTr("Levels") : qsTr("Blur size (px)")
                                from: effectPicker.previewType === "posterize_linear_rgb" ? 2 : 0
                                to: effectPicker.previewType === "posterize_linear_rgb" ? 256 : 4096
                                stepSize: effectPicker.previewType === "posterize_linear_rgb" || effectPicker.previewType === "box_blur" ? 1 : 0.25
                                value: effectPicker.previewType === "posterize_linear_rgb"
                                    ? effectPicker.previewNumber("levels", 4)
                                    : effectPicker.previewType === "box_blur"
                                        ? effectPicker.previewNumber("radius", 8)
                                        : effectPicker.previewNumber("sigma", 8)
                                valueText: effectPicker.previewType === "posterize_linear_rgb" ? Number(value).toFixed(0) : Number(value).toFixed(2)
                                defaultValue: effectPicker.previewType === "posterize_linear_rgb" ? 4 : 8
                                showResetButton: false
                                onInteractionStarted: root.controller.beginCoalescedEdit()
                                onInteractionFinished: root.controller.endCoalescedEdit()
                                onValueDragged: function(nextValue) {
                                    const amount = previewAmount.value / 100
                                    let changed = false
                                    if (effectPicker.previewType === "gaussian_blur")
                                        changed = root.controller.setPhotoLayerGaussianBlur(root.layerIndex, effectPicker.previewId, nextValue, amount, true)
                                    else if (effectPicker.previewType === "box_blur")
                                        changed = root.controller.setPhotoLayerBoxBlur(root.layerIndex, effectPicker.previewId, Math.round(nextValue), amount, true)
                                    else
                                        changed = root.controller.setPhotoLayerPosterize(root.layerIndex, effectPicker.previewId, Math.round(nextValue), amount, true)
                                    if (changed)
                                        root.refreshEffectPreviewParameters()
                                }
                            }
                            InspectorSlider {
                                id: previewAmount
                                objectName: "photoEffectPreviewAmount"
                                visible: effectPicker.previewId.length > 0
                                Layout.fillWidth: true
                                label: qsTr("Amount (%)")
                                from: 0
                                to: 100
                                stepSize: 0.25
                                value: effectPicker.previewNumber("amount", 1) * 100
                                valueText: Number(value).toFixed(2) + "%"
                                defaultValue: 100
                                showResetButton: false
                                onInteractionStarted: root.controller.beginCoalescedEdit()
                                onInteractionFinished: root.controller.endCoalescedEdit()
                                onValueDragged: function(nextValue) {
                                    if (root.controller.setPhotoLayerEffectAmount(root.layerIndex, effectPicker.previewId, nextValue / 100))
                                        root.refreshEffectPreviewParameters()
                                }
                            }
                            ActionButton {
                                objectName: "photoEffectPreviewMaskFromSelection"
                                visible: effectPicker.previewId.length > 0 && root.controller.hasWorkingSelection === true
                                text: qsTr("Mask from selection")
                                iconName: "masks"
                                showLeadingIcon: true
                                enabled: !root.locked
                                Layout.fillWidth: true
                                tooltipText: qsTr("Copy the working selection into an independent mask for this effect.")
                                onClicked: {
                                    if (root.controller.maskPhotoLayerEffectFromSelection(root.layerIndex, effectPicker.previewId)) {
                                        root.refreshEffectPreviewParameters()
                                        effectPicker.maskMessage = qsTr("Effect mask copied from selection.")
                                    }
                                }
                            }
                            ActionButton {
                                visible: effectPicker.previewId.length > 0 && String(effectPicker.previewParameters.maskId || "").length > 0
                                text: qsTr("Apply and edit mask")
                                iconName: "edit"
                                showLeadingIcon: true
                                enabled: !root.locked
                                Layout.fillWidth: true
                                onClicked: root.editEffectMask(effectPicker.previewId)
                            }
                            ActionButton {
                                visible: effectPicker.previewId.length > 0 && String(effectPicker.previewParameters.maskId || "").length > 0
                                text: qsTr("Remove effect mask")
                                iconName: "delete"
                                showLeadingIcon: true
                                enabled: !root.locked
                                Layout.fillWidth: true
                                onClicked: {
                                    if (root.controller.clearPhotoLayerEffectMask(root.layerIndex, effectPicker.previewId)) {
                                        root.refreshEffectPreviewParameters()
                                        effectPicker.maskMessage = qsTr("Effect mask removed.")
                                    }
                                }
                            }
                            Label {
                                visible: effectPicker.maskMessage.length > 0
                                text: effectPicker.maskMessage
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            Label {
                                visible: effectPicker.previewId.length > 0 && root.controller.hasWorkingSelection !== true &&
                                    String(effectPicker.previewParameters.maskId || "").length === 0
                                text: qsTr("Create a selection to limit this effect to part of the image.")
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
                Label {
                    visible: effectPicker.errorMessage.length > 0
                    text: effectPicker.errorMessage
                    color: theme.warning
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                Flow {
                    id: effectPreviewActions
                    visible: effectPicker.previewId.length > 0
                    Layout.fillWidth: true
                    spacing: theme.space8
                    readonly property real buttonWidth: width >= theme.scaled(500)
                        ? (width - spacing) / 2 : width
                    ActionButton {
                        objectName: "photoEffectPreviewCancel"
                        text: effectPicker.browsing ? qsTr("Cancel preview") : qsTr("Cancel")
                        iconName: "close"
                        showLeadingIcon: true
                        tone: "quiet"
                        width: effectPreviewActions.buttonWidth
                        onClicked: effectPicker.close()
                    }
                    ActionButton {
                        text: qsTr("Apply")
                        iconName: "check"
                        showLeadingIcon: true
                        tone: "primary"
                        objectName: "photoEffectPreviewApply"
                        visible: !effectPicker.browsing
                        enabled: effectPicker.previewId.length > 0
                        width: effectPreviewActions.buttonWidth
                        onClicked: {
                            if (root.controller.finishPhotoLayerEffectPreview(true)) {
                                root.expandedEffectId = effectPicker.previewId
                                effectPicker.close()
                            }
                        }
                    }
                }
            }
        }
    }
    Repeater {
        model: effectRowsModel
        delegate: Item {
            id: entry
            required property var effectData
            required property int index
            readonly property var modelData: effectData
            readonly property bool gaussian: modelData.type === "gaussian_blur" && modelData.version === 1
            readonly property bool box: modelData.type === "box_blur" && modelData.version === 1
            readonly property bool posterize: modelData.type === "posterize_linear_rgb" && modelData.version === 1
            readonly property bool spatial: (modelData.type === "motion_blur" || modelData.type === "dragged_shutter" || modelData.type === "unsharp_mask" || modelData.type === "optical_diffusion" || modelData.type === "imperfect_print" || modelData.type === "film_grain" || modelData.type === "light_leak" || modelData.type === "split_tone" || modelData.type === "vignette" || modelData.type === "duotone" || modelData.type === "soft_fade") && modelData.version === 1
            readonly property bool extended: Boolean(modelData.extended)
            readonly property bool supported: spatial || extended || gaussian || box || posterize || (modelData.type === "invert_linear_rgb" && modelData.version === 1)
            readonly property string title: extended ? String(modelData.name || modelData.type) : spatial ? (modelData.type === "motion_blur" ? qsTr("Motion blur") : modelData.type === "dragged_shutter" ? qsTr("Dragged shutter") : modelData.type === "optical_diffusion" ? qsTr("Optical diffusion") : modelData.type === "imperfect_print" ? qsTr("Imperfect print") : modelData.type === "film_grain" ? qsTr("Film grain") : modelData.type === "light_leak" ? qsTr("Light leak") : modelData.type === "split_tone" ? qsTr("Split tone") : modelData.type === "vignette" ? qsTr("Vignette") : modelData.type === "duotone" ? qsTr("Duotone") : modelData.type === "soft_fade" ? qsTr("Soft fade") : qsTr("Unsharp mask")) : gaussian ? qsTr("Gaussian blur") : box ? qsTr("Box blur") : posterize ? qsTr("Posterize (linear RGB)") : (supported ? qsTr("Invert (linear RGB)") : String(modelData.type))
            readonly property real amount: Number.isFinite(Number(modelData.amount)) ? Number(modelData.amount) : 1
            readonly property bool expanded: root.expandedEffectId === String(modelData.id)
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.width
            implicitWidth: 0
            implicitHeight: cardContents.implicitHeight + cardContents.y * 2
            Rectangle {
                anchors.fill: parent
                radius: root.timelineStyle ? theme.scaled(3) : theme.radiusPanel
                color: theme.alpha(theme.ink850, root.timelineStyle ? 0.22 : entry.modelData.enabled ? 0.7 : 0.42)
                border.width: root.timelineStyle ? 0 : theme.hairlineWidth
                border.color: entry.expanded ? theme.lineStrong : theme.lineSoft
            }
            ColumnLayout {
                id: cardContents
                x: root.compactCards ? theme.space6 : theme.space10
                y: x
                width: Math.max(0, parent.width - x * 2)
                spacing: root.compactCards ? theme.space4 : theme.space8
                Flow {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: Math.max(0, root.width -
                        2 * (root.compactCards ? theme.space6 : theme.space10))
                    spacing: theme.space4
                    ActionButton {
                        objectName: "photoEffectExpand_" + String(entry.modelData.id)
                        width: !root.timelineStyle && root.width < theme.scaled(360) ? cardContents.width
                            : Math.max(theme.scaled(70), cardContents.width - stateToggle.implicitWidth
                                       - (root.timelineStyle ? timelineEffectActions.implicitWidth : 0) - theme.space4 * 2)
                        text: root.width < theme.scaled(130) ? entry.title.replace(/\s/, "\n") : entry.title
                        iconName: root.width < theme.scaled(130) ? ""
                            : entry.expanded ? "chevron-down" : "chevron-right"
                        showLeadingIcon: root.width >= theme.scaled(130)
                        allowLabelWrap: true
                        checkable: true
                        checked: entry.expanded
                        active: entry.expanded && !root.timelineStyle
                        embedded: root.timelineStyle
                        alignTextLeft: root.timelineStyle
                        Accessible.name: qsTr("%1 settings").arg(entry.title)
                        tooltipText: entry.expanded ? qsTr("Collapse effect controls") : qsTr("Edit effect settings")
                        onClicked: root.expandedEffectId = entry.expanded ? "" : String(entry.modelData.id)
                    }
                    ToolOptionToggle {
                        id: stateToggle
                        objectName: "photoEffectEnabled_" + String(entry.modelData.id)
                        width: implicitWidth
                        height: implicitHeight
                        label: entry.modelData.enabled ? qsTr("On") : qsTr("Off")
                        helperText: qsTr("Bypass %1 while keeping its settings.").arg(entry.title)
                        Accessible.name: qsTr("Enable %1").arg(entry.title)
                        checked: entry.modelData.enabled
                        toggleEnabled: !root.locked
                        onToggled: function(enabled) {
                            root.controller.setPhotoLayerEffectEnabled(root.layerIndex,
                                entry.modelData.id, enabled)
                        }
                    }
                    MenuButton {
                        id: timelineEffectActions
                        objectName: "timelineEffectActions_" + String(entry.modelData.id)
                        visible: root.timelineStyle
                        text: qsTr("%1 actions").arg(entry.title)
                        accessibleName: text
                        iconName: "menu"
                        showText: false
                        compact: true
                        primaryOpensMenu: true
                        enabled: !root.locked
                        menuItems: [
                            {key: "reset", label: qsTr("Reset parameters"), icon: "reset"},
                            {key: "earlier", label: qsTr("Move earlier"), icon: "move-up", enabled: entry.index > 0},
                            {key: "later", label: qsTr("Move later"), icon: "move-down", enabled: entry.index + 1 < root.effects.length},
                            {key: "duplicate", label: qsTr("Duplicate effect"), icon: "duplicate"},
                            {key: "mask", label: qsTr("Mask from selection"), icon: "masks", enabled: root.controller.hasWorkingSelection === true},
                            {key: "edit-mask", label: qsTr("Edit effect mask"), icon: "edit", enabled: String(entry.modelData.maskId || "").length > 0},
                            {key: "clear-mask", label: qsTr("Remove effect mask"), icon: "masks", enabled: String(entry.modelData.maskId || "").length > 0},
                            {key: "remove", label: qsTr("Remove effect"), icon: "delete", separatorBefore: true}
                        ]
                        onMenuItemTriggered: function(key) {
                            if (key === "reset") root.controller.resetPhotoLayerEffect(root.layerIndex, entry.modelData.id)
                            else if (key === "earlier" || key === "later") root.controller.movePhotoLayerEffect(
                                root.layerIndex, entry.modelData.id, entry.index + (key === "earlier" ? -1 : 1))
                            else if (key === "duplicate") root.controller.duplicatePhotoLayerEffect(root.layerIndex, entry.modelData.id)
                            else if (key === "mask") root.controller.maskPhotoLayerEffectFromSelection(root.layerIndex, entry.modelData.id)
                            else if (key === "edit-mask") root.editEffectMask(entry.modelData.id)
                            else if (key === "clear-mask") root.controller.clearPhotoLayerEffectMask(root.layerIndex, entry.modelData.id)
                            else if (key === "remove") root.controller.removePhotoLayerEffect(root.layerIndex, entry.modelData.id)
                        }
                    }
                }
                Flow {
                    objectName: "photoEffectCardStatusRow_" + String(entry.modelData.id)
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: Math.max(0, root.width -
                        2 * (root.compactCards ? theme.space6 : theme.space10))
                    spacing: theme.space4
                    Rectangle {
                        objectName: "photoEffectState_" + String(entry.modelData.id)
                        visible: !root.timelineStyle
                        width: implicitWidth
                        height: implicitHeight
                        implicitWidth: stateText.implicitWidth + theme.space10
                        implicitHeight: stateText.implicitHeight + theme.space4
                        radius: theme.radiusChip
                        color: entry.modelData.enabled ? theme.editorSelectionFill : theme.ink800
                        border.width: theme.hairlineWidth
                        border.color: theme.lineSoft
                        Accessible.role: Accessible.StaticText
                        Accessible.name: qsTr("%1 is %2").arg(entry.title).arg(stateText.text)
                        Label {
                            id: stateText
                            objectName: "photoEffectStateText_" + String(entry.modelData.id)
                            anchors.centerIn: parent
                            text: entry.modelData.enabled ? qsTr("Enabled") : qsTr("Bypassed")
                            color: entry.modelData.enabled ? theme.textMain : theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                    }
                    Rectangle {
                        objectName: "photoEffectMask_" + String(entry.modelData.id)
                        width: implicitWidth
                        height: implicitHeight
                        visible: String(entry.modelData.maskId || "").length > 0
                        implicitWidth: maskContents.implicitWidth + theme.space10
                        implicitHeight: maskContents.implicitHeight + theme.space4
                        radius: theme.radiusChip
                        color: theme.ink800
                        border.width: theme.hairlineWidth
                        border.color: theme.lineSoft
                        Accessible.role: Accessible.StaticText
                        Accessible.name: qsTr("Effect mask attached")
                        RowLayout {
                            id: maskContents
                            anchors.centerIn: parent
                            spacing: theme.space2
                            AppIcon {
                                Layout.preferredWidth: theme.scaled(14)
                                Layout.preferredHeight: theme.scaled(14)
                                name: "masks"
                                color: theme.textMuted
                            }
                            Label {
                                text: qsTr("Masked")
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    objectName: "photoEffectOrderRow_" + String(entry.modelData.id)
                    visible: !root.timelineStyle && root.effects.length > 1
                    Layout.fillWidth: true
                    spacing: theme.space4
                    Label {
                        objectName: "photoEffectOrderLabel_" + String(entry.modelData.id)
                        text: qsTr("%1 of %2").arg(entry.index + 1).arg(root.effects.length)
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                    MiniButton {
                        objectName: "photoEffectEarlier_" + String(entry.modelData.id)
                        square: true
                        iconName: "move-up"
                        showLeadingIcon: true
                        Accessible.name: qsTr("Move %1 earlier").arg(entry.title)
                        tooltip: Accessible.name
                        enabled: !root.locked && entry.index > 0
                        onClicked: root.controller.movePhotoLayerEffect(root.layerIndex,
                            entry.modelData.id, entry.index - 1)
                    }
                    MiniButton {
                        objectName: "photoEffectLater_" + String(entry.modelData.id)
                        square: true
                        iconName: "move-down"
                        showLeadingIcon: true
                        Accessible.name: qsTr("Move %1 later").arg(entry.title)
                        tooltip: Accessible.name
                        enabled: !root.locked && entry.index + 1 < root.effects.length
                        onClicked: root.controller.movePhotoLayerEffect(root.layerIndex,
                            entry.modelData.id, entry.index + 1)
                    }
                }
                Flow {
                    id: amountControls
                    visible: !root.timelineStyle || entry.expanded
                    objectName: "photoGaussianAmountResponsiveGroup_" + String(entry.modelData.id)
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: Math.max(0, root.width -
                        2 * (root.compactCards ? theme.space6 : theme.space10))
                    spacing: theme.space4
                    InspectorSlider {
                        objectName: "photoGaussianAmountField"
                        width: root.timelineStyle ? amountControls.width
                            : amountControls.width >= amountReset.implicitWidth + theme.scaled(200)
                            ? amountControls.width - amountReset.implicitWidth - amountControls.spacing
                            : amountControls.width
                        label: qsTr("Amount (%)")
                        stackedHeader: root.width < theme.scaled(300)
                        from: 0
                        to: 100
                        stepSize: 0.25
                        value: entry.amount * 100
                        valueText: Number(value).toFixed(2) + "%"
                        valueColumnWidth: theme.inspectorValueWidth
                        defaultValue: 100
                        enabled: !root.locked && entry.supported
                        showResetButton: false
                        onInteractionStarted: root.beginEffectAmountEdit()
                        onInteractionFinished: root.finishEffectAmountEdit()
                        Component.onDestruction: root.finishEffectAmountEdit()
                        onValueDragged: function(nextValue) {
                            if (root.controller.setPhotoLayerEffectAmount(root.layerIndex,
                                    entry.modelData.id, nextValue / 100))
                                root.refreshEffects()
                        }
                    }
                    ActionButton {
                        id: amountReset
                        visible: !root.timelineStyle
                        objectName: "photoGaussianAmountReset"
                        text: qsTr("Reset")
                        iconName: "reset"
                        showLeadingIcon: true
                        width: implicitWidth
                        tone: "quiet"
                        enabled: !root.locked && entry.supported
                        Accessible.name: qsTr("Reset effect parameters")
                        onClicked: root.controller.resetPhotoLayerEffect(root.layerIndex, entry.modelData.id)
                    }
                }
            Label {
                visible: entry.expanded && !entry.supported
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("This effect version is unavailable. Its saved settings are preserved.")
            }
            Label {
                visible: entry.expanded && entry.supported
                Layout.fillWidth: true
                text: qsTr("Detailed settings")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: Font.DemiBold
            }
            PhotoSpatialEffectControls {
                visible: entry.expanded && entry.spatial
                Layout.fillWidth: true
                effectType: String(entry.modelData.type)
                parameters: entry.modelData
                controller: root.controller
                layerIndex: root.layerIndex
                effectId: String(entry.modelData.id)
                locked: root.locked
                onParametersEdited: function(values) {
                    root.editSpatialEffect(effectType, entry.modelData.id, values, entry.amount, entry.modelData.enabled)
                }
            }
            PhotoSpatialEffectControls {
                visible: entry.expanded && entry.extended
                Layout.fillWidth: true
                effectType: String(entry.modelData.type)
                parameters: entry.modelData
                controller: root.controller
                layerIndex: root.layerIndex
                effectId: String(entry.modelData.id)
                extended: true
                locked: root.locked
                onParametersEdited: function(values) {
                    if (root.editExtendedEffect(effectType, effectId, values,
                            entry.amount, entry.modelData.enabled))
                        root.refreshEffects()
                }
                onRefreshRequested: root.refreshEffects()
            }
            InspectorSlider {
                visible: entry.expanded && entry.gaussian
                objectName: "photoGaussianSizeField"
                Layout.fillWidth: true
                label: qsTr("Blur size (σ px)")
                from: 0
                to: 4096
                stepSize: 0.25
                value: Number(entry.modelData.sigma)
                valueText: Number(value).toFixed(2)
                defaultValue: 8
                enabled: !root.locked
                showResetButton: false
                onInteractionStarted: root.controller.beginCoalescedEdit()
                onInteractionFinished: root.controller.endCoalescedEdit()
                onValueDragged: function(nextValue) {
                    root.controller.setPhotoLayerGaussianBlur(root.layerIndex, entry.modelData.id,
                        nextValue, entry.amount, entry.modelData.enabled)
                }
            }
            InspectorSlider {
                visible: entry.expanded && entry.box
                objectName: "photoBoxRadiusField"
                Layout.fillWidth: true
                label: qsTr("Radius (px)")
                from: 0
                to: 4096
                stepSize: 1
                value: Number(entry.modelData.radius)
                valueText: Number(value).toFixed(0)
                defaultValue: 8
                enabled: !root.locked
                showResetButton: false
                onInteractionStarted: root.controller.beginCoalescedEdit()
                onInteractionFinished: root.controller.endCoalescedEdit()
                onValueDragged: function(nextValue) {
                    root.controller.setPhotoLayerBoxBlur(root.layerIndex, entry.modelData.id,
                        Math.round(nextValue), entry.amount, entry.modelData.enabled)
                }
            }
            InspectorSlider {
                visible: entry.expanded && entry.posterize
                objectName: "photoPosterizeLevelsField"
                Layout.fillWidth: true
                label: qsTr("Levels")
                from: 2
                to: 256
                stepSize: 1
                value: Number(entry.modelData.levels)
                valueText: Number(value).toFixed(0)
                defaultValue: 4
                enabled: !root.locked
                showResetButton: false
                onInteractionStarted: root.controller.beginCoalescedEdit()
                onInteractionFinished: root.controller.endCoalescedEdit()
                onValueDragged: function(nextValue) {
                    root.controller.setPhotoLayerPosterize(root.layerIndex, entry.modelData.id,
                        Math.round(nextValue), entry.amount, entry.modelData.enabled)
                }
            }
            ToneComboBox {
                visible: entry.expanded && (entry.gaussian || entry.box || (entry.spatial && entry.modelData.type !== "optical_diffusion" && entry.modelData.type !== "imperfect_print" && entry.modelData.type !== "dragged_shutter"))
                enabled: !root.locked
                Layout.fillWidth: true
                accessibleLabel: qsTr("Blur edge handling")
                model: [qsTr("Transparent edges"), qsTr("Extend edges"), qsTr("Reflect edges")]
                currentIndex: Number(entry.modelData.edgeMode)
                onActivated: function(index) {
                    root.controller.setPhotoLayerEffectEdgeMode(root.layerIndex, entry.modelData.id, index)
                }
            }
            ActionButton {
                visible: !root.timelineStyle && entry.expanded
                text: qsTr("Duplicate effect")
                iconName: "duplicate"
                showLeadingIcon: true
                enabled: !root.locked
                onClicked: root.controller.duplicatePhotoLayerEffect(root.layerIndex, entry.modelData.id)
            }
            ActionButton {
                visible: !root.timelineStyle && entry.expanded
                text: qsTr("Mask from selection")
                iconName: "masks"
                showLeadingIcon: true
                enabled: !root.locked && root.controller.hasWorkingSelection === true
                tooltipText: qsTr("Copy the working selection into an independent mask for this effect.")
                onClicked: root.controller.maskPhotoLayerEffectFromSelection(root.layerIndex, entry.modelData.id)
            }
            ActionButton {
                visible: !root.timelineStyle && entry.expanded && String(entry.modelData.maskId || "").length > 0
                text: qsTr("Edit effect mask")
                objectName: "photoEffectEditMask_" + String(entry.modelData.id)
                iconName: "edit"
                showLeadingIcon: true
                enabled: !root.locked
                onClicked: root.editEffectMask(entry.modelData.id)
            }
            ActionButton {
                visible: !root.timelineStyle && entry.expanded && String(entry.modelData.maskId || "").length > 0
                text: qsTr("Remove effect mask")
                iconName: "delete"
                showLeadingIcon: true
                tone: "danger"
                enabled: !root.locked
                onClicked: root.controller.clearPhotoLayerEffectMask(root.layerIndex, entry.modelData.id)
            }
            RowLayout {
                visible: !root.timelineStyle && entry.expanded
                Layout.fillWidth: true
                ActionButton {
                    text: qsTr("Remove")
                    iconName: "delete"
                    showLeadingIcon: true
                    tone: "danger"
                    enabled: !root.locked
                    onClicked: root.controller.removePhotoLayerEffect(root.layerIndex, entry.modelData.id)
                }
            }

            }
        }
    }
}
