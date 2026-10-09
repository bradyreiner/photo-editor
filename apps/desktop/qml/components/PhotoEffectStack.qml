pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

ColumnLayout {
    id: root
    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    property string expandedEffectId: ""
    property int effectsRevision: 0
    onLayerIndexChanged: {
        expandedEffectId = ""
        effectPicker.close()
    }
    onLockedChanged: { if (locked) effectPicker.close() }
    Component.onDestruction: root.controller.finishPhotoLayerEffectPreview(false)
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
    function refreshEffects() { effectsRevision += 1 }
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
    spacing: 6

    ActionButton {
        id: addEffectButton
        objectName: "photoAddEffectButton"
        text: qsTr("Add filter")
        enabled: !root.locked
        Layout.fillWidth: true
        onClicked: effectPicker.open()
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
        Popup {
            id: effectPicker
            objectName: "photoEffectPicker"
            parent: Overlay.overlay
            readonly property real availableOverlayWidth: Overlay.overlay ? Overlay.overlay.width : 720
            readonly property real availableOverlayHeight: Overlay.overlay ? Overlay.overlay.height : 720
            width: Math.min(Math.max(320, root.width), Math.max(1, availableOverlayWidth - 24))
            property point anchorPosition: Qt.point(0, 0)
            x: Math.max(12, Math.min(anchorPosition.x + root.width - width, availableOverlayWidth - width - 12))
            y: Math.max(12, Math.min(anchorPosition.y + addEffectButton.height + 6, availableOverlayHeight - height - 12))
            function resetScroll() {
                if (effectPickerBodyScrollView.contentItem &&
                        typeof effectPickerBodyScrollView.contentItem.contentY === "number")
                    effectPickerBodyScrollView.contentItem.contentY = 0
            }
            onBrowsingChanged: Qt.callLater(resetScroll)
            onSelectedCategoryChanged: Qt.callLater(resetScroll)
            function updateAnchor() {
                if (parent) anchorPosition = addEffectButton.mapToItem(parent, 0, 0)
            }
            height: Math.min(implicitHeight, Math.max(176, availableOverlayHeight - 24))
            padding: 12
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
            }
            onOpened: {
                updateAnchor()
                effectSearch.forceActiveFocus()
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
                        text: qsTr("Group")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                    }
                    ToneComboBox {
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
                RowLayout {
                    visible: effectPicker.previewId.length > 0
                    Layout.fillWidth: true
                    Label {
                        objectName: "photoEffectPreviewName"
                        Layout.fillWidth: true
                        text: qsTr("Preview: %1").arg(effectPicker.previewName)
                        color: theme.textStrong
                        wrapMode: Text.WordWrap
                    }
                    MiniButton {
                        objectName: "photoEffectChangeFilterButton"
                        text: effectPicker.browsing ? qsTr("Edit preview") : qsTr("Change filter")
                        onClicked: effectPicker.browsing = !effectPicker.browsing
                    }
                }
                ScrollView {
                    id: effectPickerBodyScrollView
                    objectName: effectPicker.browsing ? "photoEffectPickerBodyScrollView" : "photoEffectSettingsScrollView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    Layout.preferredHeight: Math.min(effectPickerBody.implicitHeight, 420)
                    implicitHeight: Math.min(effectPickerBody.implicitHeight, 420)
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
                            Repeater {
                                model: effectPicker.effectGroups
                                delegate: ColumnLayout {
                                    id: effectGroup
                                    required property var modelData
                                    objectName: "photoEffectGroup_" + String(modelData.category)
                                    Layout.fillWidth: true
                                    spacing: theme.space4
                                    Label {
                                        text: effectGroup.modelData.category
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeLabel
                                        font.weight: Font.DemiBold
                                    }
                                    Repeater {
                                        model: effectGroup.modelData.rows
                                        delegate: ActionButton {
                                            id: choice
                                            required property var modelData
                                            objectName: "photoEffectChoice_" + String(modelData.type)
                                            Layout.fillWidth: true
                                            text: String(modelData.name)
                                            tooltipText: String(modelData.description)
                                            active: effectPicker.previewType === String(modelData.type)
                                            onClicked: {
                                                const added = root.controller.previewPhotoLayerEffect(root.layerIndex, choice.modelData.type)
                                                if (added) {
                                                    const rows = root.controller.photoLayerEffects(root.layerIndex)
                                                    effectPicker.previewId = String(rows[rows.length - 1].id)
                                                    effectPicker.previewType = String(choice.modelData.type)
                                                    effectPicker.previewName = String(choice.modelData.name)
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
                            Label {
                                visible: effectPicker.lookChoices.length > 0
                                text: qsTr("Looks · adds editable effects immediately")
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeLabel
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: effectPicker.lookGroups
                                delegate: ColumnLayout {
                                    id: lookGroup
                                    required property var modelData
                                    objectName: "photoEffectLookGroup_" + String(modelData.category)
                                    Layout.fillWidth: true
                                    spacing: theme.space4
                                    Label {
                                        text: lookGroup.modelData.category
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeLabel
                                        font.weight: Font.DemiBold
                                    }
                                    Repeater {
                                        model: lookGroup.modelData.rows
                                        delegate: ActionButton {
                                            required property var modelData
                                            objectName: "photoEffectLookChoice_" + String(modelData.id)
                                            Layout.fillWidth: true
                                            text: String(modelData.name)
                                            tooltipText: String(modelData.description)
                                            enabled: !root.locked && typeof root.controller.applyPhotoLayerEffectLook === "function"
                                            onClicked: {
                                                if (root.controller.applyPhotoLayerEffectLook(root.layerIndex, String(modelData.id)))
                                                    effectPicker.close()
                                                else
                                                    effectPicker.errorMessage = qsTr("This look could not be added to the selected layer.")
                                            }
                                        }
                                    }
                                }
                            }
                            Label {
                                visible: effectPicker.choices.length === 0 && effectPicker.lookChoices.length === 0
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
                                visible: effectPicker.previewId.length > 0 && root.controller.activeMaskToolRole === "selection"
                                text: qsTr("Mask from selection")
                                enabled: !root.locked
                                Layout.fillWidth: true
                                tooltipText: qsTr("Copy the active selection into an independent mask for this effect.")
                                onClicked: {
                                    if (root.controller.maskPhotoLayerEffectFromSelection(root.layerIndex, effectPicker.previewId)) {
                                        root.refreshEffectPreviewParameters()
                                        effectPicker.maskMessage = qsTr("Effect mask copied from selection.")
                                    }
                                }
                            }
                            ActionButton {
                                visible: effectPicker.previewId.length > 0 && String(effectPicker.previewParameters.maskId || "").length > 0
                                text: qsTr("Edit effect mask")
                                enabled: !root.locked
                                Layout.fillWidth: true
                                onClicked: root.controller.selectPhotoLayerEffectMask(root.layerIndex, effectPicker.previewId)
                            }
                            ActionButton {
                                visible: effectPicker.previewId.length > 0 && String(effectPicker.previewParameters.maskId || "").length > 0
                                text: qsTr("Remove effect mask")
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
                                visible: effectPicker.previewId.length > 0 && root.controller.activeMaskToolRole !== "selection" &&
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
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        objectName: "photoEffectPreviewCancel"
                        text: qsTr("Cancel")
                        tone: "quiet"
                        Layout.fillWidth: true
                        onClicked: effectPicker.close()
                    }
                    ActionButton {
                        text: qsTr("Apply")
                        tone: "primary"
                        objectName: "photoEffectPreviewApply"
                        enabled: effectPicker.previewId.length > 0
                        Layout.fillWidth: true
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
        model: root.effects
        delegate: ColumnLayout {
            id: entry
            required property var modelData
            required property int index
            readonly property bool gaussian: modelData.type === "gaussian_blur" && modelData.version === 1
            readonly property bool box: modelData.type === "box_blur" && modelData.version === 1
            readonly property bool posterize: modelData.type === "posterize_linear_rgb" && modelData.version === 1
            readonly property bool spatial: (modelData.type === "motion_blur" || modelData.type === "dragged_shutter" || modelData.type === "unsharp_mask" || modelData.type === "optical_diffusion" || modelData.type === "imperfect_print" || modelData.type === "film_grain" || modelData.type === "light_leak" || modelData.type === "split_tone" || modelData.type === "vignette" || modelData.type === "duotone" || modelData.type === "soft_fade") && modelData.version === 1
            readonly property bool extended: Boolean(modelData.extended)
            readonly property bool supported: spatial || extended || gaussian || box || posterize || (modelData.type === "invert_linear_rgb" && modelData.version === 1)
            readonly property string title: extended ? String(modelData.name || modelData.type) : spatial ? (modelData.type === "motion_blur" ? qsTr("Motion blur") : modelData.type === "dragged_shutter" ? qsTr("Dragged shutter") : modelData.type === "optical_diffusion" ? qsTr("Optical diffusion") : modelData.type === "imperfect_print" ? qsTr("Imperfect print") : modelData.type === "film_grain" ? qsTr("Film grain") : modelData.type === "light_leak" ? qsTr("Light leak") : modelData.type === "split_tone" ? qsTr("Split tone") : modelData.type === "vignette" ? qsTr("Vignette") : modelData.type === "duotone" ? qsTr("Duotone") : modelData.type === "soft_fade" ? qsTr("Soft fade") : qsTr("Unsharp mask")) : gaussian ? qsTr("Gaussian blur") : box ? qsTr("Box blur") : posterize ? qsTr("Posterize (linear RGB)") : (supported ? qsTr("Invert (linear RGB)") : String(modelData.type))
            readonly property bool expanded: root.expandedEffectId === String(modelData.id)
            Layout.fillWidth: true
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    objectName: "photoEffectExpand_" + String(entry.modelData.id)
                    Layout.fillWidth: true
                    text: qsTr("%1. %2").arg(entry.index + 1).arg(entry.title)
                    checkable: true
                    checked: entry.expanded
                    active: entry.expanded
                    tooltipText: entry.expanded ? qsTr("Collapse effect controls") : qsTr("Edit effect settings")
                    onClicked: root.expandedEffectId = entry.expanded ? "" : String(entry.modelData.id)
                }
                ToolOptionToggle {
                    objectName: "photoEffectEnabled_" + String(entry.modelData.id)
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
            }
            Label {
                visible: entry.expanded && !entry.supported
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("This effect version is unavailable. Its saved settings are preserved.")
            }
            RowLayout {
                visible: entry.expanded && entry.supported
                Layout.fillWidth: true
                InspectorSlider {
                    objectName: "photoGaussianAmountField"
                    Layout.fillWidth: true
                    label: qsTr("Amount (%)")
                    from: 0
                    to: 100
                    stepSize: 0.25
                    value: Number(entry.modelData.amount * 100)
                    valueText: Number(value).toFixed(2) + "%"
                    defaultValue: 100
                    enabled: !root.locked
                    showResetButton: false
                    onInteractionStarted: root.controller.beginCoalescedEdit()
                    onInteractionFinished: root.controller.endCoalescedEdit()
                    onValueDragged: function(nextValue) {
                        root.controller.setPhotoLayerEffectAmount(root.layerIndex,
                            entry.modelData.id, nextValue / 100)
                    }
                }
                ActionButton {
                    text: qsTr("Reset")
                    tone: "quiet"
                    enabled: !root.locked
                    Accessible.name: qsTr("Reset effect parameters")
                    onClicked: root.controller.resetPhotoLayerEffect(root.layerIndex, entry.modelData.id)
                }
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
                    root.editSpatialEffect(effectType, entry.modelData.id, values, entry.modelData.amount, entry.modelData.enabled)
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
                            entry.modelData.amount, entry.modelData.enabled))
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
                        nextValue, entry.modelData.amount, entry.modelData.enabled)
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
                        Math.round(nextValue), entry.modelData.amount, entry.modelData.enabled)
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
                        Math.round(nextValue), entry.modelData.amount, entry.modelData.enabled)
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
                visible: entry.expanded
                text: qsTr("Duplicate effect")
                enabled: !root.locked
                onClicked: root.controller.duplicatePhotoLayerEffect(root.layerIndex, entry.modelData.id)
            }
            ActionButton {
                visible: entry.expanded
                text: qsTr("Mask from selection")
                enabled: !root.locked && root.controller.activeMaskToolRole === "selection"
                tooltipText: qsTr("Copy the active selection into an independent mask for this effect.")
                onClicked: root.controller.maskPhotoLayerEffectFromSelection(root.layerIndex, entry.modelData.id)
            }
            ActionButton {
                visible: entry.expanded && String(entry.modelData.maskId || "").length > 0
                text: qsTr("Edit effect mask")
                enabled: !root.locked
                onClicked: root.controller.selectPhotoLayerEffectMask(root.layerIndex, entry.modelData.id)
            }
            ActionButton {
                visible: entry.expanded && String(entry.modelData.maskId || "").length > 0
                text: qsTr("Remove effect mask")
                tone: "danger"
                enabled: !root.locked
                onClicked: root.controller.clearPhotoLayerEffectMask(root.layerIndex, entry.modelData.id)
            }
            RowLayout {
                visible: entry.expanded && root.effects.length > 1
                Label { text: qsTr("Effect %1").arg(entry.index + 1); Layout.fillWidth: true }
                ActionButton {
                    text: qsTr("Earlier")
                    compact: true
                    enabled: !root.locked && entry.index > 0
                    onClicked: root.controller.movePhotoLayerEffect(root.layerIndex,
                        entry.modelData.id, entry.index - 1)
                }
                ActionButton {
                    text: qsTr("Later")
                    compact: true
                    enabled: !root.locked && entry.index + 1 < root.effects.length
                    onClicked: root.controller.movePhotoLayerEffect(root.layerIndex,
                        entry.modelData.id, entry.index + 1)
                }
            }
            RowLayout {
                visible: entry.expanded
                Layout.fillWidth: true
                ActionButton {
                    text: qsTr("Remove")
                    tone: "danger"
                    enabled: !root.locked
                    onClicked: root.controller.removePhotoLayerEffect(root.layerIndex, entry.modelData.id)
                }
            }


        }
    }
}
