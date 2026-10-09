pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "."

ColumnLayout {
    id: root
    Theme { id: uiTheme }
    required property string effectType
    required property var parameters
    required property var controller
    required property int layerIndex
    required property string effectId
    property bool extended: false
    property bool locked: false
    property int selectedGradientStop: -1
    property string extendedError: ""
    signal parametersEdited(var values)
    signal refreshRequested()
    readonly property var extendedFields: root.extended && typeof root.controller.photoEffectParameterFields === "function"
        ? root.controller.photoEffectParameterFields(root.effectType) : []
    readonly property var gradientStops: asArray(root.parameters.gradientStops)

    function asArray(value) {
        const rows = []
        if (value && typeof value.length === "number")
            for (let i = 0; i < value.length; ++i) rows.push(value[i])
        return rows
    }

    function fieldValue(field) {
        const key = String(field.key)
        const current = Number(root.parameters[key])
        return Number.isFinite(current) ? current : Number(field.defaultValue || 0)
    }
    function scalarValues() {
        const values = ({})
        for (let i = 0; i < root.extendedFields.length; ++i) {
            const field = root.extendedFields[i]
            values[String(field.key)] = root.fieldValue(field)
        }
        return values
    }
    function copyGradientStops() {
        const source = root.gradientStops
        return source.map(function(stop) {
            const copy = {position: Number(stop.position), color: String(stop.color || "#000000")}
            if (root.asArray(stop.linearRgb).length === 3) copy.linearRgb = root.asArray(stop.linearRgb)
            return copy
        })
    }
    function commitExtended(values) {
        if (root.effectType === "gradient_map") values.gradientStops = root.copyGradientStops()
        root.extendedError = ""
        root.parametersEdited(values)
    }
    function editExtendedScalar(key, value) {
        const values = root.scalarValues()
        values[key] = value
        root.commitExtended(values)
    }
    function editGradientStopPosition(index, value) {
        const stops = root.copyGradientStops()
        if (index < 0 || index >= stops.length) return
        const lower = index > 0 ? stops[index - 1].position + 0.000001 : 0
        const upper = index + 1 < stops.length ? stops[index + 1].position - 0.000001 : 1
        stops[index].position = Math.max(lower, Math.min(upper, Number(value)))
        const values = root.scalarValues()
        values.gradientStops = stops
        root.extendedError = ""
        root.parametersEdited(values)
    }
    function openGradientStopColor(index) {
        if (index < 0 || index >= root.gradientStops.length) return
        root.selectedGradientStop = index
        gradientColorDialog.selectedColor = String(root.gradientStops[index].color || "#000000")
        gradientColorDialog.open()
    }
    function addGradientStop() {
        const stops = root.copyGradientStops()
        if (stops.length >= 32) return
        let bestIndex = 0
        let bestGap = -1
        for (let i = 0; i + 1 < stops.length; ++i) {
            const gap = stops[i + 1].position - stops[i].position
            if (gap > bestGap) { bestGap = gap; bestIndex = i }
        }
        if (bestGap <= 0.000002) return
        const left = stops[bestIndex]
        const right = stops[bestIndex + 1]
        const midpoint = (left.position + right.position) / 2
        const added = {position: midpoint, color: root.approximateMidpointColor(left.color, right.color)}
        if (Array.isArray(left.linearRgb) && left.linearRgb.length === 3 &&
                Array.isArray(right.linearRgb) && right.linearRgb.length === 3) {
            added.linearRgb = left.linearRgb.map(function(channel, index) {
                return (Number(channel) + Number(right.linearRgb[index])) * 0.5
            })
        }
        stops.splice(bestIndex + 1, 0, added)
        const values = root.scalarValues()
        values.gradientStops = stops
        root.extendedError = ""
        root.parametersEdited(values)
    }
    function approximateMidpointColor(leftColor, rightColor) {
        function channels(color) {
            const hex = String(color || "#000000").replace(/^#/, "")
            if (!/^[0-9a-fA-F]{6}$/.test(hex)) return [0, 0, 0]
            return [parseInt(hex.slice(0, 2), 16), parseInt(hex.slice(2, 4), 16), parseInt(hex.slice(4, 6), 16)]
        }
        const left = channels(leftColor)
        const right = channels(rightColor)
        const middle = left.map(function(channel, index) { return Math.round((channel + right[index]) * 0.5) })
        return "#" + middle.map(function(channel) { return channel.toString(16).padStart(2, "0") }).join("")
    }
    function removeGradientStop(index) {
        const stops = root.copyGradientStops()
        if (stops.length <= 2 || index < 0 || index >= stops.length) return
        stops.splice(index, 1)
        const values = root.scalarValues()
        values.gradientStops = stops
        root.extendedError = ""
        root.parametersEdited(values)
    }
    function importDisplacementMap(url) {
        const changed = root.controller.setPhotoLayerDisplacementMap(root.layerIndex, root.effectId, String(url))
        root.extendedError = changed ? "" : qsTr("Could not import this displacement map.")
        if (changed) root.refreshRequested()
    }
    function clearDisplacementMap() {
        const changed = root.controller.setPhotoLayerDisplacementMap(root.layerIndex, root.effectId, "")
        root.extendedError = changed ? "" : qsTr("Could not clear this displacement map.")
        if (changed) root.refreshRequested()
    }
    readonly property var fields: effectType === "film_grain" ? [
        {key: "grainSize", label: qsTr("Grain size (px)"), minimum: 0.25, maximum: 64, step: 0.05, defaultValue: 1},
        {key: "grainStrength", label: qsTr("Grain strength (%)"), minimum: 0, maximum: 12, step: 0.05, scale: 100, defaultValue: 2},
        {key: "grainColor", label: qsTr("Color variation (%)"), minimum: -100, maximum: 100, step: 1, scale: 100, defaultValue: -80},
        {key: "seed", label: qsTr("Grain seed"), minimum: 0, maximum: 4294967295, step: 1, defaultValue: 1}
    ] : effectType === "light_leak" ? [
        {key: "leakCenterX", label: qsTr("Center X (%)"), minimum: -100, maximum: 200, step: 0.25, scale: 100, defaultValue: 82},
        {key: "leakCenterY", label: qsTr("Center Y (%)"), minimum: -100, maximum: 200, step: 0.25, scale: 100, defaultValue: 28},
        {key: "leakAngle", label: qsTr("Angle (°)"), minimum: -180, maximum: 180, step: 1, defaultValue: 0},
        {key: "leakWidth", label: qsTr("Spread (%)"), minimum: 2, maximum: 200, step: 0.25, scale: 100, defaultValue: 30},
        {key: "leakStrength", label: qsTr("Light intensity (%)"), minimum: 0, maximum: 100, step: 0.25, scale: 100, defaultValue: 25},
        {key: "warmth", label: qsTr("Warmth / coolness (%)"), minimum: -100, maximum: 100, step: 1, scale: 100, defaultValue: 80},
        {key: "seed", label: qsTr("Variation seed"), minimum: 0, maximum: 4294967295, step: 1, defaultValue: 1}
    ] : effectType === "split_tone" ? [
        {key: "shadowHue", label: qsTr("Shadow hue (°)"), minimum: 0, maximum: 360, step: 1, defaultValue: 32},
        {key: "shadowStrength", label: qsTr("Shadow tint (%)"), minimum: 0, maximum: 100, step: 0.25, scale: 100, defaultValue: 12},
        {key: "highlightHue", label: qsTr("Highlight hue (°)"), minimum: 0, maximum: 360, step: 1, defaultValue: 215},
        {key: "highlightStrength", label: qsTr("Highlight tint (%)"), minimum: 0, maximum: 100, step: 0.25, scale: 100, defaultValue: 8},
        {key: "balance", label: qsTr("Tone balance (%)"), minimum: -100, maximum: 100, step: 1, scale: 100, defaultValue: 0}
    ] : effectType === "vignette" ? [
        {key: "vignetteRadius", label: qsTr("Radius (%)"), minimum: 0, maximum: 200, step: 0.25, scale: 100, defaultValue: 72},
        {key: "roundness", label: qsTr("Shape (%)"), minimum: -100, maximum: 100, step: 1, scale: 100, defaultValue: 0},
        {key: "feather", label: qsTr("Feather (%)"), minimum: 0, maximum: 200, step: 0.25, scale: 100, defaultValue: 36},
        {key: "vignetteStrength", label: qsTr("Edge darkening (%)"), minimum: 0, maximum: 200, step: 0.25, scale: 100, defaultValue: 35}
    ] : effectType === "duotone" ? [
        {key: "shadowHue", label: qsTr("Shadow hue (°)"), minimum: 0, maximum: 360, step: 1, defaultValue: 250},
        {key: "highlightHue", label: qsTr("Highlight hue (°)"), minimum: 0, maximum: 360, step: 1, defaultValue: 42},
        {key: "duotoneStrength", label: qsTr("Color mapping (%)"), minimum: 0, maximum: 100, step: 0.25, scale: 100, defaultValue: 80},
        {key: "balance", label: qsTr("Tone balance (%)"), minimum: -100, maximum: 100, step: 1, scale: 100, defaultValue: 0}
    ] : effectType === "soft_fade" ? [
        {key: "lift", label: qsTr("Shadow lift (%)"), minimum: 0, maximum: 30, step: 0.25, scale: 100, defaultValue: 3.5},
        {key: "contrast", label: qsTr("Contrast (%)"), minimum: 0, maximum: 200, step: 0.25, scale: 100, defaultValue: 92},
        {key: "saturation", label: qsTr("Saturation (%)"), minimum: 0, maximum: 200, step: 0.25, scale: 100, defaultValue: 92}
    ] : effectType === "imperfect_print" ? [
        {key: "paperScale", label: qsTr("Paper scale (px)"), minimum: 2, maximum: 512, step: 1, defaultValue: 32},
        {key: "paperTexture", label: qsTr("Paper texture (%)"), minimum: 0, maximum: 100, step: 0.25, scale: 100, defaultValue: 20},
        {key: "inkSpread", label: qsTr("Ink spread (%)"), minimum: 0, maximum: 100, step: 0.25, scale: 100, defaultValue: 0},
        {key: "inkDensity", label: qsTr("Ink density (%)"), minimum: 0, maximum: 200, step: 0.25, scale: 100, defaultValue: 100},
        {key: "redRegistrationX", label: qsTr("Red plate X (px)"), minimum: -8, maximum: 8, step: 0.05, defaultValue: 0},
        {key: "redRegistrationY", label: qsTr("Red plate Y (px)"), minimum: -8, maximum: 8, step: 0.05, defaultValue: 0},
        {key: "blueRegistrationX", label: qsTr("Blue plate X (px)"), minimum: -8, maximum: 8, step: 0.05, defaultValue: 0},
        {key: "blueRegistrationY", label: qsTr("Blue plate Y (px)"), minimum: -8, maximum: 8, step: 0.05, defaultValue: 0},
        {key: "seed", label: qsTr("Texture seed"), minimum: 0, maximum: 4294967295, step: 1, defaultValue: 1}
    ] : effectType === "optical_diffusion" ? [
        {key: "diffusionRadius", label: qsTr("Spread (px)"), minimum: 0, maximum: 1024, step: 0.25, defaultValue: 24},
        {key: "highlightThreshold", label: qsTr("Highlight threshold"), minimum: 0, maximum: 16, step: 0.01, defaultValue: 0.8},
        {key: "highlightKnee", label: qsTr("Highlight softness"), minimum: 0, maximum: 8, step: 0.01, defaultValue: 0.25},
        {key: "diffusionStrength", label: qsTr("Glow strength (%)"), minimum: 0, maximum: 300, step: 0.25, scale: 100, defaultValue: 80},
        {key: "warmth", label: qsTr("Glow warmth"), minimum: -100, maximum: 100, step: 1, scale: 100, defaultValue: 20},
        {key: "highlightProtection", label: qsTr("Highlight protection (%)"), minimum: 0, maximum: 100, step: 1, scale: 100, defaultValue: 0}
    ] : effectType === "motion_blur" ? [
        {key: "distance", label: qsTr("Distance (px)"), minimum: 0, maximum: 4096, step: 0.25, defaultValue: 20},
        {key: "angle", label: qsTr("Angle (°)"), minimum: -180, maximum: 180, step: 1, defaultValue: 0}
    ] : effectType === "dragged_shutter" ? [
        {key: "distance", label: qsTr("Trail length (px)"), minimum: 0, maximum: 4096, step: 0.25, defaultValue: 32},
        {key: "angle", label: qsTr("Trail angle (°)"), minimum: -180, maximum: 180, step: 1, defaultValue: 0},
        {key: "falloff", label: qsTr("Edge falloff (%)"), minimum: 0, maximum: 100, step: 1, scale: 100, defaultValue: 65},
        {key: "highlightWeighting", label: qsTr("Highlight weighting (%)"), minimum: 0, maximum: 100, step: 1, scale: 100, defaultValue: 0}
    ] : [
        {key: "sharpenRadius", label: qsTr("Radius (σ px)"), minimum: 0, maximum: 4096, step: 0.25, defaultValue: 2},
        {key: "strength", label: qsTr("Strength (%)"), minimum: 0, maximum: 500, step: 0.25, scale: 100, defaultValue: 100},
        {key: "threshold", label: qsTr("Threshold (%)"), minimum: 0, maximum: 100, step: 0.25, scale: 100, defaultValue: 0}
    ]
    Repeater {
        model: root.extended ? [] : root.fields
        delegate: InspectorSlider {
            id: row
            required property var modelData
            readonly property real scale: Number(modelData.scale || 1)
            objectName: "photoEffectParameter_" + modelData.key
            Layout.fillWidth: true
            forceCompact: root.effectType === "optical_diffusion" || root.effectType === "imperfect_print" ||
                root.effectType === "film_grain" || root.effectType === "light_leak" ||
                root.effectType === "split_tone" || root.effectType === "vignette" ||
                root.effectType === "duotone" || root.effectType === "soft_fade"
            controlHeight: forceCompact ? uiTheme.scaled(46) : uiTheme.scaled(62)
            label: modelData.label
            from: Number(modelData.minimum)
            to: Number(modelData.maximum)
            stepSize: Number(modelData.step || 0.25)
            value: Number(root.parameters[modelData.key] || 0) * scale
            valueText: Number(value).toFixed(scale > 1 ? 2 : (stepSize % 1 === 0 ? 0 : 2))
            defaultValue: Number(modelData.defaultValue || 0)
            enabled: !root.locked
            showResetButton: false
            onInteractionStarted: root.controller.beginCoalescedEdit()
            onInteractionFinished: root.controller.endCoalescedEdit()
            onValueDragged: function(nextValue) {
                const values = Object.assign({}, root.parameters)
                values[row.modelData.key] = nextValue / row.scale
                root.parametersEdited(values)
            }
        }
    }
    ColumnLayout {
        visible: root.extended
        Layout.fillWidth: true
        spacing: uiTheme.space4
        Repeater {
            model: root.extendedFields
            delegate: ColumnLayout {
                id: extendedField
                required property var modelData
                readonly property var choices: root.asArray(modelData.choices)
                readonly property real minimum: Number(modelData.minimum)
                readonly property real maximum: Number(modelData.maximum)
                readonly property real step: Number(modelData.step || 0.01)
                readonly property real displayScale: Number(modelData.scale || 1)
                readonly property real displayStep: Math.round(step * displayScale * 1000000) / 1000000
                readonly property int precision: Math.max(0, Math.min(6,
                    String(displayStep).indexOf(".") < 0 ? 0 : String(displayStep).length - String(displayStep).indexOf(".") - 1))
                visible: root.effectType !== "dither" || String(modelData.key) !== "matrix_size" || Number(root.parameters.mode) === 0
                Layout.fillWidth: true
                spacing: uiTheme.space2
                InspectorSlider {
                    visible: extendedField.choices.length === 0
                    objectName: "photoExtendedEffectParameter_" + String(extendedField.modelData.key)
                    Layout.fillWidth: true
                    forceCompact: false
                    controlHeight: uiTheme.scaled(62)
                    label: String(extendedField.modelData.label)
                    from: extendedField.minimum * extendedField.displayScale
                    to: extendedField.maximum * extendedField.displayScale
                    stepSize: extendedField.displayStep
                    value: root.fieldValue(extendedField.modelData) * extendedField.displayScale
                    valueText: Number(value).toFixed(extendedField.precision)
                    defaultValue: Number(extendedField.modelData.defaultValue || 0) * extendedField.displayScale
                    enabled: !root.locked
                    showResetButton: false
                    onInteractionStarted: root.controller.beginCoalescedEdit()
                    onInteractionFinished: root.controller.endCoalescedEdit()
                    onValueDragged: function(nextValue) {
                        root.editExtendedScalar(String(extendedField.modelData.key), nextValue / extendedField.displayScale)
                    }
                }
                Label {
                    visible: extendedField.choices.length > 0
                    text: String(extendedField.modelData.label)
                    color: uiTheme.textMuted
                    font.family: uiTheme.fontUi
                    font.pixelSize: uiTheme.typeCaption
                    Layout.fillWidth: true
                }
                ToneComboBox {
                    visible: extendedField.choices.length > 0
                    objectName: "photoExtendedEffectChoice_" + String(extendedField.modelData.key)
                    Layout.fillWidth: true
                    enabled: !root.locked
                    accessibleLabel: String(extendedField.modelData.label)
                    model: extendedField.choices
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: {
                        const current = String(root.fieldValue(extendedField.modelData))
                        for (let i = 0; i < extendedField.choices.length; ++i) {
                            if (String(extendedField.choices[i].value) === current) return i
                        }
                        return 0
                    }
                    onActivated: function(index) {
                        if (index >= 0 && index < extendedField.choices.length)
                            root.editExtendedScalar(String(extendedField.modelData.key), extendedField.choices[index].value)
                    }
                }
            }
        }
        ColumnLayout {
            visible: root.effectType === "gradient_map"
            Layout.fillWidth: true
            spacing: uiTheme.space4
            Label {
                text: qsTr("Gradient stops")
                color: uiTheme.textMuted
                font.family: uiTheme.fontUi
                font.pixelSize: uiTheme.typeCaption
            }
            Repeater {
                model: root.gradientStops
                delegate: RowLayout {
                    id: gradientStopRow
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    InspectorSlider {
                        objectName: "photoGradientMapStopPosition_" + String(gradientStopRow.index)
                        Layout.fillWidth: true
                        forceCompact: false
                        controlHeight: uiTheme.scaled(62)
                        label: qsTr("Stop %1").arg(gradientStopRow.index + 1)
                        from: 0
                        to: 1
                        stepSize: 0.001
                        value: Number(gradientStopRow.modelData.position)
                        valueText: Number(value).toFixed(3)
                        defaultValue: Number(gradientStopRow.modelData.position)
                        enabled: !root.locked
                        showResetButton: false
                        onInteractionStarted: root.controller.beginCoalescedEdit()
                        onInteractionFinished: root.controller.endCoalescedEdit()
                        onValueDragged: function(nextValue) {
                            root.editGradientStopPosition(gradientStopRow.index, nextValue)
                        }
                    }
                    ColorSwatchButton {
                        objectName: "photoGradientMapStopColor_" + String(gradientStopRow.index)
                        swatchColor: String(gradientStopRow.modelData.color || "#000000")
                        label: qsTr("Gradient stop %1 color").arg(gradientStopRow.index + 1)
                        enabled: !root.locked
                        onPicked: root.openGradientStopColor(gradientStopRow.index)
                    }
                    MiniButton {
                        objectName: "photoGradientMapRemoveStop_" + String(gradientStopRow.index)
                        square: true
                        iconName: "close"
                        role: "icon"
                        tooltip: qsTr("Remove gradient stop")
                        enabled: !root.locked && root.gradientStops.length > 2
                        onClicked: root.removeGradientStop(gradientStopRow.index)
                    }
                }
            }
            ActionButton {
                objectName: "photoGradientMapAddStop"
                text: qsTr("Add stop")
                enabled: !root.locked && root.gradientStops.length < 32
                Layout.fillWidth: true
                onClicked: root.addGradientStop()
            }
        }
        ColumnLayout {
            visible: root.effectType === "displacement_map"
            Layout.fillWidth: true
            spacing: uiTheme.space4
            Label {
                Layout.fillWidth: true
                text: String(root.parameters.mapName || qsTr("No displacement map selected"))
                color: uiTheme.textMuted
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                font.family: uiTheme.fontUi
                font.pixelSize: uiTheme.typeCaption
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    objectName: "photoDisplacementMapChoose"
                    text: qsTr("Choose map…")
                    enabled: !root.locked
                    Layout.fillWidth: true
                    onClicked: displacementMapDialog.open()
                }
                ActionButton {
                    objectName: "photoDisplacementMapClear"
                    text: qsTr("Clear")
                    tone: "quiet"
                    visible: String(root.parameters.mapName || "").length > 0
                    enabled: !root.locked
                    onClicked: root.clearDisplacementMap()
                }
            }
        }
        Label {
            visible: root.extendedError.length > 0
            text: root.extendedError
            color: uiTheme.warning
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }
    ColorDialog {
        id: gradientColorDialog
        title: qsTr("Gradient stop color")
        onAccepted: {
            const stops = root.copyGradientStops()
            if (root.selectedGradientStop < 0 || root.selectedGradientStop >= stops.length) return
            stops[root.selectedGradientStop].color = String(selectedColor)
            delete stops[root.selectedGradientStop].linearRgb
            const values = root.scalarValues()
            values.gradientStops = stops
            root.extendedError = ""
            root.parametersEdited(values)
        }
    }
    FileDialog {
        id: displacementMapDialog
        title: qsTr("Choose a displacement map")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.tif *.tiff *.bmp)"), qsTr("All files (*)")]
        onAccepted: root.importDisplacementMap(String(selectedFile))
    }
    ToneCheckBox {
        objectName: "photoEffectProtectSubjectCheckBox"
        visible: root.effectType === "dragged_shutter"
        enabled: !root.locked && String(root.parameters.maskId || "").length > 0
        text: qsTr("Protect masked subject")
        accessibleLabel: qsTr("Protect masked subject")
        accessibleDescription: qsTr("Keep pixels covered by this effect mask sharp while the shutter trail affects the surrounding image.")
        checked: Boolean(root.parameters.protectSubject)
        onToggled: {
            const values = Object.assign({}, root.parameters)
            values.protectSubject = checked
            root.parametersEdited(values)
        }
    }
}
