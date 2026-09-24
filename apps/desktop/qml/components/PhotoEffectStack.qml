pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

ColumnLayout {
    id: root
    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    property string expandedEffectId: ""
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
        return revision && root.layerIndex >= 0 ? root.controller.photoLayerEffects(root.layerIndex) : []
    }
    function editSpatialEffect(type, id, values, amount, enabled) {
        if (type === "motion_blur")
            return controller.setPhotoLayerMotionBlur(layerIndex, id, values.distance, values.angle, amount, enabled)
        return controller.setPhotoLayerUnsharpMask(layerIndex, id, values.sharpenRadius,
            values.strength, values.threshold, amount, enabled)
    }
    function refreshEffectPreviewParameters() {
        const rows = controller.photoLayerEffects(layerIndex)
        for (let i = 0; i < rows.length; ++i) {
            if (String(rows[i].id) === effectPicker.previewId) {
                effectPicker.previewParameters = rows[i]
                return true
            }
        }
        return false
    }
    spacing: 6

    Button {
        objectName: "photoAddEffectButton"
        text: qsTr("Add effect")
        enabled: !root.locked
        Layout.fillWidth: true
        onClicked: effectPicker.open()
    }
    Popup {
        id: effectPicker
        objectName: "photoEffectPicker"
        implicitWidth: Math.max(240, root.width)
        padding: 12
        modal: true
        focus: true
        property string previewId: ""
        property string previewType: ""
        property string previewName: ""
        property var previewParameters: ({})
        readonly property bool spatialPreview: previewType === "motion_blur" || previewType === "unsharp_mask"
        property string errorMessage: ""
        property string maskMessage: ""
        function previewNumber(key, fallback) {
            const value = Number(previewParameters[key])
            return Number.isFinite(value) ? value : fallback
        }
        onClosed: {
            root.controller.finishPhotoLayerEffectPreview(false)
            previewId = ""
            previewType = ""
            previewName = ""
            errorMessage = ""
            maskMessage = ""
        }
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: effectSearch.forceActiveFocus()
        readonly property var choices: root.controller.availablePhotoEffects().filter(function(effect) {
            return (String(effect.name) + " " + String(effect.category)).toLowerCase()
                .indexOf(effectSearch.text.trim().toLowerCase()) >= 0
        })
        contentItem: ColumnLayout {
            spacing: 8
            TextField {
                id: effectSearch
                Layout.fillWidth: true
                placeholderText: qsTr("Search effects")
                Accessible.name: qsTr("Search effects")
                selectByMouse: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(effectChoices.implicitHeight, 180)
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    id: effectChoices
                    width: parent.width
                    Repeater {
                        model: effectPicker.choices
                        delegate: Button {
                            id: choice
                            objectName: "photoEffectChoice_" + String(modelData.type)
                            required property var modelData
                            Layout.fillWidth: true
                            text: String(modelData.name)
                            ToolTip.visible: hovered
                            ToolTip.text: String(modelData.description)
                            onClicked: {
                                const added = root.controller.previewPhotoLayerEffect(root.layerIndex, choice.modelData.type)
                                if (added) {
                                    const rows = root.controller.photoLayerEffects(root.layerIndex)
                                    effectPicker.previewId = String(rows[rows.length - 1].id)
                                    effectPicker.previewType = String(choice.modelData.type)
                                    effectPicker.previewName = String(choice.modelData.name)
                                    effectPicker.previewParameters = rows[rows.length - 1]
                                    effectPicker.errorMessage = ""
                                    effectPicker.maskMessage = ""
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
                visible: effectPicker.previewId.length > 0
                text: qsTr("Preview: %1").arg(effectPicker.previewName)
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            PhotoSpatialEffectControls {
                visible: effectPicker.previewId.length > 0 && effectPicker.spatialPreview
                Layout.fillWidth: true
                effectType: effectPicker.previewType
                parameters: effectPicker.previewParameters
                controller: root.controller
                onParametersEdited: function(values) {
                    if (root.editSpatialEffect(effectType, effectPicker.previewId, values, previewAmount.value / 100, true))
                        effectPicker.previewParameters = values
                }
            }
            InspectorSlider {
                id: previewSize
                objectName: "photoEffectPreviewSize"
                visible: effectPicker.previewId.length > 0 && effectPicker.previewType !== "invert_linear_rgb" && !effectPicker.spatialPreview
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
            Button {
                visible: effectPicker.previewId.length > 0 && root.controller.activeMaskToolRole === "selection"
                text: qsTr("Mask from selection")
                enabled: !root.locked
                Layout.fillWidth: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy the active selection into an independent mask for this effect.")
                onClicked: {
                    if (root.controller.maskPhotoLayerEffectFromSelection(root.layerIndex, effectPicker.previewId)) {
                        root.refreshEffectPreviewParameters()
                        effectPicker.maskMessage = qsTr("Effect mask copied from selection.")
                    }
                }
            }
            Button {
                visible: effectPicker.previewId.length > 0 && String(effectPicker.previewParameters.maskId || "").length > 0
                text: qsTr("Edit effect mask")
                enabled: !root.locked
                Layout.fillWidth: true
                onClicked: root.controller.selectPhotoLayerEffectMask(root.layerIndex, effectPicker.previewId)
            }
            Button {
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
            Label {
                visible: effectPicker.errorMessage.length > 0
                text: effectPicker.errorMessage
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    onClicked: effectPicker.close()
                }
                Button {
                    text: qsTr("Apply")
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
            Label {
                visible: effectPicker.choices.length === 0
                text: qsTr("No matching effects")
                Layout.fillWidth: true
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
            readonly property bool spatial: (modelData.type === "motion_blur" || modelData.type === "unsharp_mask") && modelData.version === 1
            readonly property bool supported: spatial || gaussian || box || posterize || (modelData.type === "invert_linear_rgb" && modelData.version === 1)
            readonly property string title: spatial ? (modelData.type === "motion_blur" ? qsTr("Motion blur") : qsTr("Unsharp mask")) : gaussian ? qsTr("Gaussian blur") : box ? qsTr("Box blur") : posterize ? qsTr("Posterize (linear RGB)") : (supported ? qsTr("Invert (linear RGB)") : String(modelData.type))
            readonly property bool expanded: root.expandedEffectId === String(modelData.id)
            Layout.fillWidth: true
            Button {
                Layout.fillWidth: true
                text: qsTr("%1. %2").arg(entry.index + 1).arg(entry.title)
                    + (entry.modelData.enabled ? "" : qsTr(" · Off"))
                checkable: true
                checked: entry.expanded
                Accessible.name: text + (entry.expanded ? qsTr(", collapse controls") : qsTr(", edit effect"))
                onClicked: root.expandedEffectId = entry.expanded ? "" : String(entry.modelData.id)
            }
            Label {
                visible: entry.expanded && !entry.supported
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("This effect version is unavailable. Its saved settings are preserved.")
            }
            ComboBox {
                visible: entry.expanded && (entry.gaussian || entry.box || entry.spatial)
                enabled: !root.locked
                Layout.fillWidth: true
                Accessible.name: qsTr("Blur edge handling")
                model: [qsTr("Transparent edges"), qsTr("Extend edges"), qsTr("Reflect edges")]
                currentIndex: Number(entry.modelData.edgeMode)
                onActivated: function(index) {
                    root.controller.setPhotoLayerEffectEdgeMode(root.layerIndex, entry.modelData.id, index)
                }
            }
            Button {
                visible: entry.expanded
                text: qsTr("Duplicate effect")
                enabled: !root.locked
                onClicked: root.controller.duplicatePhotoLayerEffect(root.layerIndex, entry.modelData.id)
            }
            Button {
                visible: entry.expanded
                text: qsTr("Mask from selection")
                enabled: !root.locked && root.controller.activeMaskToolRole === "selection"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy the active selection into an independent mask for this effect.")
                onClicked: root.controller.maskPhotoLayerEffectFromSelection(root.layerIndex, entry.modelData.id)
            }
            Button {
                visible: entry.expanded && String(entry.modelData.maskId || "").length > 0
                text: qsTr("Edit effect mask")
                enabled: !root.locked
                onClicked: root.controller.selectPhotoLayerEffectMask(root.layerIndex, entry.modelData.id)
            }
            Button {
                visible: entry.expanded && String(entry.modelData.maskId || "").length > 0
                text: qsTr("Remove effect mask")
                enabled: !root.locked
                onClicked: root.controller.clearPhotoLayerEffectMask(root.layerIndex, entry.modelData.id)
            }
            RowLayout {
                visible: entry.expanded && root.effects.length > 1
                Label { text: qsTr("Effect %1").arg(entry.index + 1); Layout.fillWidth: true }
                Button {
                    text: qsTr("Earlier")
                    enabled: !root.locked && entry.index > 0
                    onClicked: root.controller.movePhotoLayerEffect(root.layerIndex,
                        entry.modelData.id, entry.index - 1)
                }
                Button {
                    text: qsTr("Later")
                    enabled: !root.locked && entry.index + 1 < root.effects.length
                    onClicked: root.controller.movePhotoLayerEffect(root.layerIndex,
                        entry.modelData.id, entry.index + 1)
                }
            }
            RowLayout {
                visible: entry.expanded
                Layout.fillWidth: true
                CheckBox {
                    text: entry.title
                    checked: entry.modelData.enabled
                    enabled: !root.locked
                    Layout.fillWidth: true
                    onClicked: root.controller.setPhotoLayerEffectEnabled(root.layerIndex,
                        entry.modelData.id, checked)
                }
                Button {
                    text: qsTr("Remove")
                    enabled: !root.locked
                    onClicked: root.controller.removePhotoLayerEffect(root.layerIndex, entry.modelData.id)
                }
            }
            PhotoSpatialEffectControls {
                visible: entry.expanded && entry.spatial
                Layout.fillWidth: true
                effectType: String(entry.modelData.type)
                parameters: entry.modelData
                controller: root.controller
                locked: root.locked
                onParametersEdited: function(values) {
                    root.editSpatialEffect(effectType, entry.modelData.id, values, entry.modelData.amount, entry.modelData.enabled)
                }
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
                Button {
                    text: qsTr("Reset")
                    enabled: !root.locked
                    Accessible.name: qsTr("Reset effect parameters")
                    onClicked: root.controller.resetPhotoLayerEffect(root.layerIndex, entry.modelData.id)
                }
            }
        }
    }
}
