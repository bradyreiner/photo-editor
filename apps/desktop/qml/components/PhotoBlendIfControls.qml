pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

ColumnLayout {
    id: root

    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    property bool editOpen: false

    readonly property string photoBlendIfControlsContract: "photo-blend-if-controls:v1|source-and-underlying:true|channel:gray-rgb|linear-working-range:true|feathered-splits:true|pair-order-preserved:true|reset:true|locked:true|responsive:true|coalesced-edit:true"
    readonly property bool editLocked: root.locked || (root.layerData && root.layerData.locked === true)
    readonly property bool canEdit: !root.editLocked && root.layerIndex >= 0 &&
        root.controller && typeof root.controller.setPhotoLayerBlendIf === "function" &&
        typeof root.controller.beginCoalescedEdit === "function" &&
        typeof root.controller.endCoalescedEdit === "function"
    readonly property var blendIfSettings: root.readSettings()
    readonly property bool isDefault: root.isDefaultSettings(root.blendIfSettings)

    Theme { id: theme }
    spacing: theme.space4

    function defaults() {
        return {
            enabled: false,
            channel: "gray",
            sourceBlackLow: 0.0,
            sourceBlackHigh: 0.0,
            sourceWhiteLow: 1.0,
            sourceWhiteHigh: 1.0,
            underlyingBlackLow: 0.0,
            underlyingBlackHigh: 0.0,
            underlyingWhiteLow: 1.0,
            underlyingWhiteHigh: 1.0
        }
    }

    function normalizedSettings(value) {
        const result = defaults()
        const source = value && typeof value === "object" ? value : ({})
        result.enabled = Boolean(source.enabled)
        const channel = String(source.channel || "gray").toLowerCase()
        result.channel = ["gray", "red", "green", "blue"].indexOf(channel) >= 0 ? channel : "gray"
        const fields = [
            "sourceBlackLow", "sourceBlackHigh", "sourceWhiteLow", "sourceWhiteHigh",
            "underlyingBlackLow", "underlyingBlackHigh",
            "underlyingWhiteLow", "underlyingWhiteHigh"
        ]
        for (let i = 0; i < fields.length; ++i) {
            const key = fields[i]
            const numeric = Number(source[key])
            if (Number.isFinite(numeric))
                result[key] = Math.max(0, Math.min(1, numeric))
        }
        const pairs = [
            ["sourceBlackLow", "sourceBlackHigh"],
            ["sourceWhiteLow", "sourceWhiteHigh"],
            ["underlyingBlackLow", "underlyingBlackHigh"],
            ["underlyingWhiteLow", "underlyingWhiteHigh"]
        ]
        for (let i = 0; i < pairs.length; ++i) {
            const lowKey = pairs[i][0]
            const highKey = pairs[i][1]
            if (result[lowKey] > result[highKey]) {
                const previousLow = result[lowKey]
                result[lowKey] = result[highKey]
                result[highKey] = previousLow
            }
        }
        return result
    }

    function readSettings() {
        const layerSettings = root.layerData && root.layerData.blendIf
        if (layerSettings && typeof layerSettings === "object")
            return normalizedSettings(layerSettings)
        if (root.controller && typeof root.controller.photoLayerBlendIf === "function") {
            const controllerSettings = root.controller.photoLayerBlendIf(root.layerIndex)
            if (controllerSettings && typeof controllerSettings === "object")
                return normalizedSettings(controllerSettings)
        }
        return defaults()
    }

    function isDefaultSettings(settings) {
        const value = normalizedSettings(settings)
        const base = defaults()
        const fields = Object.keys(base)
        for (let i = 0; i < fields.length; ++i)
            if (value[fields[i]] !== base[fields[i]]) return false
        return true
    }

    function beginEdit() {
        if (!root.canEdit || root.editOpen) return
        root.controller.beginCoalescedEdit()
        root.editOpen = true
    }

    function endEdit() {
        if (!root.editOpen) return
        root.editOpen = false
        root.controller.endCoalescedEdit()
    }

    function commitSettings(nextSettings) {
        if (!root.canEdit) return false
        const ownsEdit = !root.editOpen
        if (ownsEdit) root.beginEdit()
        let changed = false
        try {
            changed = Boolean(root.controller.setPhotoLayerBlendIf(
                root.layerIndex, root.normalizedSettings(nextSettings)))
        } finally {
            if (ownsEdit) root.endEdit()
        }
        return changed
    }

    function changeSetting(key, value) {
        const next = normalizedSettings(root.blendIfSettings)
        next[key] = value
        commitSettings(next)
    }

    function resetSettings() {
        commitSettings(defaults())
    }

    function thresholdValue(key) {
        return Number(root.blendIfSettings[key])
    }

    function defaultThreshold(key) {
        return key.endsWith("WhiteLow") || key.endsWith("WhiteHigh") ? 1.0 : 0.0
    }

    function setThreshold(key, value) {
        const next = normalizedSettings(root.blendIfSettings)
        const numeric = Number(value)
        if (!Number.isFinite(numeric)) return
        const bounded = Math.max(0.0, Math.min(1.0, numeric))
        next[key] = bounded
        // Every slider stays on the full normalized range. Move its paired
        // endpoint when needed so a split never becomes inverted.
        if (key.endsWith("Low"))
            next[key.replace(/Low$/, "High")] = Math.max(
                next[key.replace(/Low$/, "High")], bounded)
        else if (key.endsWith("High"))
            next[key.replace(/High$/, "Low")] = Math.min(
                next[key.replace(/High$/, "Low")], bounded)
        commitSettings(next)
    }

    function channelIndex(channel) {
        const index = ["gray", "red", "green", "blue"].indexOf(String(channel).toLowerCase())
        return index >= 0 ? index : 0
    }

    function channelForIndex(index) {
        const channels = ["gray", "red", "green", "blue"]
        return channels[Math.max(0, Math.min(channels.length - 1, index))]
    }

    onEditLockedChanged: if (root.editLocked) root.endEdit()
    Component.onDestruction: root.endEdit()

    Components.InspectorSection {
        objectName: "photoBlendIfSection"
        Layout.fillWidth: true
        title: qsTr("Blend If")
        subtitle: qsTr("Tonal source and backdrop gates")
        helpText: qsTr("Blend If fades this layer according to its own tone and the composite beneath it.")
        resetAvailable: root.canEdit && !root.isDefault
        resetObjectName: "photoBlendIfReset"
        resetTooltip: qsTr("Reset Blend If to its bypassed defaults.")
        changed: !root.isDefault
        onResetRequested: root.resetSettings()

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8

                Components.ToneSwitch {
                    objectName: "photoBlendIfEnable"
                    Layout.fillWidth: true
                    text: qsTr("Enable tonal gates")
                    accessibleLabel: qsTr("Enable Blend If tonal gates")
                    accessibleDescription: qsTr("When off, both source and underlying tonal gates are bypassed.")
                    checked: root.blendIfSettings.enabled
                    enabled: root.canEdit
                    onToggled: root.changeSetting("enabled", checked)
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= theme.scaled(300) ? 2 : 1
                columnSpacing: theme.space8
                rowSpacing: theme.space4

                Label {
                    objectName: "photoBlendIfChannelLabel"
                    text: qsTr("Channel")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                    Layout.fillWidth: true
                    Layout.preferredWidth: root.width >= theme.scaled(300) ? theme.scaled(72) : 0
                }

                Components.ToneComboBox {
                    objectName: "photoBlendIfChannel"
                    Layout.fillWidth: true
                    accessibleLabel: qsTr("Blend If channel")
                    model: [
                        { label: qsTr("Gray"), value: "gray" },
                        { label: qsTr("Red"), value: "red" },
                        { label: qsTr("Green"), value: "green" },
                        { label: qsTr("Blue"), value: "blue" }
                    ]
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: root.channelIndex(root.blendIfSettings.channel)
                    enabled: root.canEdit
                    onActivated: function(index) {
                        root.changeSetting("channel", root.channelForIndex(index))
                    }
                }
            }

            Label {
                objectName: "photoBlendIfSemantics"
                Layout.fillWidth: true
                text: qsTr("Values use linear working RGB. Negative values map to black; values at the layer blend headroom map to white.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }

            Label {
                objectName: "photoBlendIfFeatherHelp"
                Layout.fillWidth: true
                text: qsTr("Black low to high fades in. White low to high fades out. Source and underlying gates multiply.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= theme.scaled(760) ? 2 : 1
                columnSpacing: theme.space8
                rowSpacing: theme.space4

                Components.InspectorSection {
                    objectName: "photoBlendIfSourceSection"
                    Layout.fillWidth: true
                    title: qsTr("This layer")
                    subtitle: qsTr("Source tone")
                    helpText: qsTr("Feather the layer's own tonal contribution.")
                    expandable: true
                    expanded: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: theme.space2

                        Repeater {
                            model: [
                                { key: "sourceBlackLow", label: qsTr("Black low") },
                                { key: "sourceBlackHigh", label: qsTr("Black high") },
                                { key: "sourceWhiteLow", label: qsTr("White low") },
                                { key: "sourceWhiteHigh", label: qsTr("White high") }
                            ]

                            Components.InspectorSlider {
                                required property var modelData
                                Layout.fillWidth: true
                                objectName: "photoBlendIf_" + String(modelData.key)
                                label: String(modelData.label)
                                from: 0.0
                                to: 1.0
                                stepSize: 0.01
                                value: root.thresholdValue(String(modelData.key))
                                valueText: Math.round(value * 100) + "%"
                                defaultValue: root.defaultThreshold(String(modelData.key))
                                enabled: root.canEdit && root.blendIfSettings.enabled
                                disabledReason: root.editLocked
                                    ? qsTr("Unlock this layer to edit Blend If.")
                                    : qsTr("Enable tonal gates to edit these ranges.")
                                showResetButton: false
                                onInteractionStarted: root.beginEdit()
                                onInteractionFinished: root.endEdit()
                                onValueDragged: function(nextValue) {
                                    root.setThreshold(String(modelData.key), nextValue)
                                }
                            }
                        }
                    }
                }

                Components.InspectorSection {
                    objectName: "photoBlendIfUnderlyingSection"
                    Layout.fillWidth: true
                    title: qsTr("Underlying composite")
                    subtitle: qsTr("Visible layers below")
                    helpText: qsTr("Feather this layer according to the combined tone beneath it.")
                    expandable: true
                    expanded: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: theme.space2

                        Repeater {
                            model: [
                                { key: "underlyingBlackLow", label: qsTr("Black low") },
                                { key: "underlyingBlackHigh", label: qsTr("Black high") },
                                { key: "underlyingWhiteLow", label: qsTr("White low") },
                                { key: "underlyingWhiteHigh", label: qsTr("White high") }
                            ]

                            Components.InspectorSlider {
                                required property var modelData
                                Layout.fillWidth: true
                                objectName: "photoBlendIf_" + String(modelData.key)
                                label: String(modelData.label)
                                from: 0.0
                                to: 1.0
                                stepSize: 0.01
                                value: root.thresholdValue(String(modelData.key))
                                valueText: Math.round(value * 100) + "%"
                                defaultValue: root.defaultThreshold(String(modelData.key))
                                enabled: root.canEdit && root.blendIfSettings.enabled
                                disabledReason: root.editLocked
                                    ? qsTr("Unlock this layer to edit Blend If.")
                                    : qsTr("Enable tonal gates to edit these ranges.")
                                showResetButton: false
                                onInteractionStarted: root.beginEdit()
                                onInteractionFinished: root.endEdit()
                                onValueDragged: function(nextValue) {
                                    root.setThreshold(String(modelData.key), nextValue)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
