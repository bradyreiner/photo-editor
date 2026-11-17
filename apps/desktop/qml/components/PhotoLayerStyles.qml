pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "."

ColumnLayout {
    id: root
    objectName: "photoLayerStyles"
    required property var controller
    required property int layerIndex
    required property var layerData
    property var maskEditHandler: null
    property string canvasStyleId: ""
    property bool locked: false
    property int revision: 0
    property string expandedId: ""
    property string copiedEnvelope: ""
    property string status: ""
    property string dialogLayerId: ""
    property string colorStyleId: ""
    property var colorPath: []
    property var colorTargetSnapshot: null
    property real colorStopPosition: -1
    readonly property string layerId: String(layerData.id || "")
    readonly property var styles: {
        const data = root.layerData
        const token = root.revision
        return data && token >= 0 && root.layerIndex >= 0 ? root.controller.photoLayerStyles(root.layerIndex) : []
    }
    readonly property var types: root.controller.availablePhotoLayerStyles()
    signal styleMaskClearRequested(int layerIndex, string styleId, string maskSetId)
    signal styleCanvasEditRequested(int layerIndex, string styleId)
    spacing: theme.space8
    Layout.minimumWidth: 0
    Theme { id: theme }
    ListModel { id: rows }
    Connections {
        target: root.controller
        function onControlsChanged() { root.revision += 1 }
    }

    function syncRows() {
        const values = root.styles
        let same = rows.count === values.length
        for (let i = 0; same && i < values.length; ++i)
            same = String(rows.get(i).styleData.id) === String(values[i].id)
        if (!same) rows.clear()
        for (let i = 0; i < values.length; ++i) {
            if (same) rows.setProperty(i, "styleData", values[i])
            else rows.append({styleData: values[i]})
        }
    }
    function changed(ok) {
        root.status = ok ? "" : String(root.controller.status || qsTr("This style could not be changed. Check the layer lock and values."))
        root.revision += 1
    }
    function operation(id) {
        for (const value of root.styles)
            if (String(value.id) === String(id)) return value
        return null
    }
    function requestStyleMaskEdit(id, setId) {
        const selected = typeof root.maskEditHandler === "function"
            ? Boolean(root.maskEditHandler(root.layerIndex, setId))
            : Boolean(root.controller.selectPhotoLayerStyleMask(root.layerIndex, id))
        if (selected) {
            root.status = ""
            return true
        }
        root.status = String(root.controller.status || qsTr("This style mask could not be opened."))
        return false
    }
    function edit(id, patch) {
        if (!root.locked) root.changed(root.controller.setPhotoLayerStyleParameters(root.layerIndex, id, patch))
    }
    function readPath(value, path) {
        let target = value
        for (const key of path) {
            if (target === null || target === undefined) return undefined
            target = target[key]
        }
        return target
    }
    function writePath(value, path, replacement) {
        if (path.length === 0) return replacement
        let target = value
        for (let i = 0; i < path.length - 1; ++i) target = target[path[i]]
        target[path[path.length - 1]] = replacement
        return value
    }
    function parameter(id, path, value) {
        const style = root.operation(id)
        if (!style) return
        const parameters = JSON.parse(JSON.stringify(style.parameters))
        root.writePath(parameters, path, value)
        root.edit(id, {parameters: parameters})
    }
    function colorsMatch(left, right) {
        if (!left || !right) return false
        return Math.abs(Number(left.r) - Number(right.r)) < 1e-10 &&
                Math.abs(Number(left.g) - Number(right.g)) < 1e-10 &&
                Math.abs(Number(left.b) - Number(right.b)) < 1e-10 &&
                Math.abs(Number(left.a) - Number(right.a)) < 1e-10
    }
    function colorTargetStillMatches() {
        if (root.dialogLayerId !== root.layerId || root.locked) return false
        const style = root.operation(root.colorStyleId)
        if (!style || !root.colorTargetSnapshot) return false
        if (!root.colorsMatch(root.readPath(style.parameters, root.colorPath), root.colorTargetSnapshot.color))
            return false
        if (root.colorStopPosition >= 0) {
            const positionPath = root.colorPath.slice(0, root.colorPath.length - 1).concat(["position"])
            return Math.abs(Number(root.readPath(style.parameters, positionPath)) - root.colorStopPosition) < 1e-10
        }
        return true
    }
    function srgb(value) {
        return value <= 0.0031308 ? 12.92 * value : 1.055 * Math.pow(Math.max(0, value), 1 / 2.4) - 0.055
    }
    function linear(value) {
        return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4)
    }
    function displayColor(value) {
        return Qt.rgba(Math.max(0, Math.min(1, root.srgb(Number(value.r)))),
                       Math.max(0, Math.min(1, root.srgb(Number(value.g)))),
                       Math.max(0, Math.min(1, root.srgb(Number(value.b)))), Number(value.a))
    }
    function chooseColor(id, path, value, stopPosition) {
        root.colorStyleId = id
        root.colorPath = path
        root.dialogLayerId = root.layerId
        root.colorTargetSnapshot = {color: JSON.parse(JSON.stringify(value))}
        root.colorStopPosition = stopPosition === undefined ? -1 : Number(stopPosition)
        colorDialog.selectedColor = root.displayColor(value)
        colorDialog.open()
    }
    function canInsertGradientStop(gradient) {
        const stops = gradient && gradient.stops ? gradient.stops : []
        if (stops.length < 2 || stops.length >= 32) return false
        for (let i = 0; i < stops.length - 1; ++i)
            if (Number(stops[i + 1].position) - Number(stops[i].position) >= 0.002)
                return true
        return false
    }
    function gradientColorAt(gradient, position, ignoreReverse) {
        const stops = gradient && gradient.stops ? gradient.stops : []
        if (stops.length < 2) return {r: 1, g: 1, b: 1, a: 1}
        const value = gradient.reverse === true && ignoreReverse !== true ? 1 - position : position
        let upper = 1
        while (upper < stops.length - 1 && Number(stops[upper].position) < value) ++upper
        const left = stops[upper - 1]
        const right = stops[upper]
        const span = Number(right.position) - Number(left.position)
        const ratio = span > 0 ? Math.max(0, Math.min(1, (value - Number(left.position)) / span)) : 0
        const color = {}
        for (const key of ["r", "g", "b", "a"])
            color[key] = Number(left.color[key]) * (1 - ratio) + Number(right.color[key]) * ratio
        return color
    }
    function addGradientStop(id, path) {
        const style = root.operation(id)
        if (!style) return
        const parameters = JSON.parse(JSON.stringify(style.parameters))
        const gradient = root.readPath(parameters, path)
        const stops = gradient && gradient.stops ? gradient.stops.slice() : []
        if (stops.length >= 32) {
            root.status = qsTr("A style gradient can contain at most 32 stops.")
            return
        }
        let widestIndex = -1
        let widestGap = 0
        for (let i = 0; i < stops.length - 1; ++i) {
            const gap = Number(stops[i + 1].position) - Number(stops[i].position)
            if (gap > widestGap) { widestGap = gap; widestIndex = i }
        }
        if (widestIndex < 0 || widestGap < 0.002) {
            root.status = qsTr("There is no room for another gradient stop. Move an interior stop first.")
            return
        }
        const left = stops[widestIndex]
        const right = stops[widestIndex + 1]
        const position = Math.round(((Number(left.position) + Number(right.position)) / 2) * 100000) / 100000
        if (position <= Number(left.position) || position >= Number(right.position)) {
            root.status = qsTr("There is no room for another gradient stop. Move an interior stop first.")
            return
        }
        stops.splice(widestIndex + 1, 0, {position: position, color: root.gradientColorAt(gradient, position, true)})
        root.writePath(parameters, path.concat(["stops"]), stops)
        root.edit(id, {parameters: parameters})
    }
    function removeGradientStop(id, path, stopIndex) {
        const style = root.operation(id)
        if (!style) return
        const parameters = JSON.parse(JSON.stringify(style.parameters))
        const stops = root.readPath(parameters, path.concat(["stops"]))
        if (!stops || stopIndex <= 0 || stopIndex >= stops.length - 1) return
        stops.splice(stopIndex, 1)
        root.edit(id, {parameters: parameters})
    }
    function setStrokeFillKind(id, kind) {
        const style = root.operation(id)
        if (!style || !style.parameters.fill) return
        const parameters = JSON.parse(JSON.stringify(style.parameters))
        const current = parameters.fill
        if (current.kind === kind) return
        if (kind === "gradient") {
            const color = current.color || {r: 1, g: 1, b: 1, a: 1}
            parameters.fill = {kind: "gradient", gradient: {
                angleDegrees: 0, scale: 1, reverse: false,
                stops: [{position: 0, color: color},
                        {position: 1, color: {r: 1, g: 1, b: 1, a: 1}}]}}
        } else {
            const gradient = current.gradient || ({})
            parameters.fill = {kind: "solid", color: root.gradientColorAt(gradient, 0.5)}
        }
        root.edit(id, {parameters: parameters})
    }
    onStylesChanged: Qt.callLater(root.syncRows)
    onLayerIdChanged: {
        root.expandedId = ""
        root.status = ""
        colorDialog.close()
        importDialog.close()
        exportDialog.close()
        root.colorTargetSnapshot = null
        root.colorStopPosition = -1
    }
    onLockedChanged: {
        if (root.locked) {
            colorDialog.close()
            importDialog.close()
            exportDialog.close()
            root.colorTargetSnapshot = null
            root.colorStopPosition = -1
        }
    }
    Component.onCompleted: root.syncRows()

    component NumberRow: RowLayout {
        id: numberRow
        required property string label
        required property real number
        property real minimum: 0
        property real maximum: 1024
        property string unit: "px"
        signal edited(real number)
        Layout.fillWidth: true
        Label {
            text: numberRow.label
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeControl
            Layout.fillWidth: true
        }
        ToneSpinBox {
            id: styleNumberInput
            accessibleLabel: numberRow.label
            valueUnit: numberRow.unit
            from: Math.round(numberRow.minimum * 10)
            to: Math.round(numberRow.maximum * 10)
            editable: true
            validator: DoubleValidator {
                bottom: numberRow.minimum
                top: numberRow.maximum
                decimals: 1
                notation: DoubleValidator.StandardNotation
            }
            textFromValue: function(value, locale) { return Number(value / 10).toLocaleString(locale, 'f', 1) }
            valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 10) }
            onValueModified: numberRow.edited(value / 10)
            Binding {
                target: styleNumberInput
                property: "value"
                value: Math.round(numberRow.number * 10)
            }
        }
    }
    component ColorRow: RowLayout {
        id: colorRow
        required property string label
        required property var value
        signal choose()
        Layout.fillWidth: true
        Label {
            text: colorRow.label
            Layout.fillWidth: true
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeControl
        }
        Rectangle {
            implicitWidth: theme.scaled(24)
            implicitHeight: theme.scaled(24)
            color: root.displayColor(colorRow.value)
            border.color: theme.textMuted
            radius: theme.radius4
        }
        MiniButton {
            text: qsTr("Choose")
            Accessible.name: colorRow.label
            onClicked: colorRow.choose()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        ToneComboBox {
            id: picker
            objectName: "layerStylePicker"
            Layout.fillWidth: true
            model: root.types
            textRole: "name"
            Accessible.name: qsTr("Style to add")
            enabled: !root.locked
        }
        MiniButton {
            objectName: "addLayerStyleButton"
            text: qsTr("Add")
            iconName: "plus"
            enabled: !root.locked && picker.currentIndex >= 0
            onClicked: {
                root.changed(root.controller.addPhotoLayerStyle(root.layerIndex, String(root.types[picker.currentIndex].type)))
                if (root.styles.length) root.expandedId = String(root.styles[root.styles.length - 1].id)
            }
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: theme.space4
        MiniButton {
            text: qsTr("Copy styles")
            enabled: root.styles.length > 0
            onClicked: root.copiedEnvelope = root.controller.photoLayerStyleEnvelope(root.layerIndex)
        }
        MiniButton {
            text: qsTr("Paste styles")
            enabled: !root.locked && root.copiedEnvelope.length > 0
            onClicked: root.changed(root.controller.applyPhotoLayerStyleEnvelope(root.layerIndex, root.copiedEnvelope))
        }
        MiniButton {
            text: qsTr("Save preset…")
            enabled: root.styles.length > 0
            onClicked: { root.dialogLayerId = root.layerId; exportDialog.open() }
        }
        MiniButton {
            text: qsTr("Load preset…")
            enabled: !root.locked
            onClicked: { root.dialogLayerId = root.layerId; importDialog.open() }
        }
    }
    Label {
        visible: rows.count === 0
        text: qsTr("Add a stroke, shadow, glow or overlay. Your layer stays editable.")
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeBody
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    ToneCheckBox {
        objectName: "layerStyleDocumentLightLink"
        visible: rows.count > 0
        text: qsTr("Link to document light")
        enabled: !root.locked
        checked: {
            const data = root.layerData
            const token = root.revision
            return token >= 0 && data && root.controller.photoLayerStyleLightLinked(root.layerIndex)
        }
        onToggled: root.changed(root.controller.setPhotoLayerStyleLightLinked(root.layerIndex, checked))
    }
    NumberRow {
        id: styleLightNumberRow
        visible: rows.count > 0
        label: root.layerData.layerStyleLightLinked === true ? qsTr("Document light") : qsTr("Layer light")
        number: {
            const data = root.layerData
            const token = root.revision
            return token >= 0 && data ? root.controller.photoLayerStyleLightAngle(root.layerIndex) : -45
        }
        minimum: -3600
        maximum: 3600
        unit: qsTr("degrees")
        enabled: !root.locked
        onEdited: number => root.changed(root.controller.setPhotoLayerStyleLightAngle(root.layerIndex, number))
        HoverHandler { id: lightHover }
        AppToolTip {
            targetItem: styleLightNumberRow
            text: qsTr("Used by styles with Shared light enabled. Linked layers share the document's angle. 0° points right; 90° points down.")
            visible: styleLightNumberRow.visible && lightHover.hovered
        }
    }
    Repeater {
        model: rows
        delegate: ColumnLayout {
            id: card
            required property int index
            required property var styleData
            readonly property var parameters: styleData.parameters || ({})
            readonly property bool expanded: root.expandedId === String(styleData.id)
            readonly property bool supported: styleData.supported !== false && Number(styleData.algorithmVersion) === 1
            readonly property bool canvasEditable: ["drop_shadow", "inner_shadow", "gradient_overlay"].indexOf(String(styleData.typeId)) >= 0 ||
                (String(styleData.typeId) === "stroke" && !!parameters.fill && parameters.fill.kind === "gradient")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: theme.space4
            RowLayout {
                Layout.fillWidth: true
                ToneCheckBox {
                    objectName: "layerStyleEnabled_" + String(card.styleData.id)
                    checked: card.styleData.enabled === true
                    enabled: !root.locked && (card.supported || card.styleData.enabled === true)
                    Accessible.name: qsTr("Enable %1").arg(card.styleData.name)
                    onToggled: root.changed(root.controller.setPhotoLayerStyleEnabled(root.layerIndex, String(card.styleData.id), checked))
                }
                MiniButton {
                    objectName: "layerStyleHeader_" + String(card.styleData.id)
                    text: String(card.styleData.name)
                    Layout.fillWidth: true
                    onClicked: root.expandedId = card.expanded ? "" : String(card.styleData.id)
                }
                MiniButton {
                    objectName: "layerStyleUp_" + String(card.styleData.id)
                    text: qsTr("↑")
                    iconName: "chevron-up"
                    labelVisible: false
                    tooltip: qsTr("Move style up")
                    Accessible.name: qsTr("Move style up")
                    enabled: !root.locked && card.index > 0
                    onClicked: root.changed(root.controller.movePhotoLayerStyle(root.layerIndex, String(card.styleData.id), card.index - 1))
                }
                MiniButton {
                    objectName: "layerStyleDown_" + String(card.styleData.id)
                    text: qsTr("↓")
                    iconName: "chevron-down"
                    labelVisible: false
                    tooltip: qsTr("Move style down")
                    Accessible.name: qsTr("Move style down")
                    enabled: !root.locked && card.index < rows.count - 1
                    onClicked: root.changed(root.controller.movePhotoLayerStyle(root.layerIndex, String(card.styleData.id), card.index + 1))
                }
                MiniButton {
                    objectName: "layerStyleRemove_" + String(card.styleData.id)
                    text: qsTr("×")
                    iconName: "trash"
                    labelVisible: false
                    tooltip: qsTr("Remove style")
                    Accessible.name: qsTr("Remove style")
                    enabled: !root.locked
                    onClicked: root.changed(root.controller.removePhotoLayerStyle(root.layerIndex, String(card.styleData.id)))
                }
            }
            ColumnLayout {
                visible: card.expanded
                enabled: !root.locked && card.supported
                Layout.fillWidth: true
                MiniButton {
                    objectName: "layerStyleCanvasEdit_" + String(card.styleData.id)
                    visible: card.canvasEditable
                    text: root.canvasStyleId === String(card.styleData.id) ? qsTr("Done on canvas") : qsTr("Edit on canvas")
                    enabled: !root.locked && card.styleData.enabled !== false
                    onClicked: root.styleCanvasEditRequested(root.layerIndex, String(card.styleData.id))
                }
                NumberRow {
                    label: qsTr("Opacity")
                    number: Number(card.styleData.opacity) * 100
                    maximum: 100
                    unit: "%"
                    onEdited: number => root.edit(String(card.styleData.id), {opacity: number / 100})
                }
                RowLayout {
                    Label { text: qsTr("Blend"); color: theme.textMuted; Layout.fillWidth: true }
                    ToneComboBox {
                        model: ["Normal", "Multiply", "Screen", "Overlay", "Soft light"]
                        readonly property var keys: ["normal", "multiply", "screen", "overlay", "softLight"]
                        currentIndex: keys.indexOf(String(card.styleData.blendMode))
                        Accessible.name: qsTr("Style blend mode")
                        onActivated: root.edit(String(card.styleData.id), {blendMode: keys[currentIndex]})
                    }
                }
                RowLayout {
                    Label { text: qsTr("Placement"); color: theme.textMuted; Layout.fillWidth: true }
                    ToneComboBox {
                        readonly property bool overlay: String(card.styleData.type) === "color_overlay" || String(card.styleData.type) === "gradient_overlay"
                        model: overlay ? [qsTr("On artwork")] : [qsTr("Behind artwork"), qsTr("On artwork"), qsTr("In front")]
                        readonly property var keys: overlay ? ["onContent"] : ["belowContent", "onContent", "aboveContent"]
                        currentIndex: keys.indexOf(String(card.styleData.plane))
                        Accessible.name: qsTr("Style placement")
                        onActivated: root.edit(String(card.styleData.id), {plane: keys[currentIndex]})
                    }
                }
                Repeater {
                    model: [
                        {key: "width", label: qsTr("Width"), maximum: 1024},
                        {key: "distance", label: qsTr("Distance"), maximum: 1024},
                        {key: "blur", label: qsTr("Blur"), maximum: 1024},
                        {key: "spread", label: qsTr("Spread"), maximum: 1024},
                        {key: "size", label: qsTr("Size"), maximum: 1024},
                        {key: "softness", label: qsTr("Softness"), maximum: 1024},
                        {key: "depth", label: qsTr("Depth"), maximum: 16}]
                    delegate: NumberRow {
                        required property var modelData
                        visible: card.parameters[modelData.key] !== undefined
                        label: modelData.label
                        number: Number(card.parameters[modelData.key] || 0)
                        maximum: modelData.maximum
                        unit: modelData.key === "depth" ? "" : "px"
                        onEdited: number => root.parameter(String(card.styleData.id), [modelData.key], number)
                    }
                }
                ToneCheckBox {
                    visible: card.parameters.useGlobalLight !== undefined
                    text: qsTr("Shared light")
                    checked: card.parameters.useGlobalLight === true
                    onToggled: root.parameter(String(card.styleData.id), ["useGlobalLight"], checked)
                }
                NumberRow {
                    visible: card.parameters.angleDegrees !== undefined
                    enabled: card.parameters.useGlobalLight !== true
                    label: qsTr("Angle")
                    minimum: -360
                    maximum: 360
                    unit: qsTr("degrees")
                    number: Number(card.parameters.angleDegrees || 0)
                    onEdited: number => root.parameter(String(card.styleData.id), ["angleDegrees"], number)
                }
                Repeater {
                    model: ["color", "highlight", "shadow"]
                    delegate: ColorRow {
                        required property var modelData
                        visible: card.parameters[modelData] !== undefined
                        label: modelData === "highlight" ? qsTr("Highlight") : modelData === "shadow" ? qsTr("Shadow") : qsTr("Color")
                        value: card.parameters[modelData] || ({r: 0, g: 0, b: 0, a: 1})
                        onChoose: root.chooseColor(String(card.styleData.id), [modelData], value)
                    }
                }
                RowLayout {
                    visible: card.parameters.alignment !== undefined
                    Label { text: qsTr("Position"); color: theme.textMuted; Layout.fillWidth: true }
                    ToneComboBox {
                        model: [qsTr("Inside"), qsTr("Center"), qsTr("Outside")]
                        readonly property var keys: ["inside", "center", "outside"]
                        currentIndex: keys.indexOf(String(card.parameters.alignment))
                        Accessible.name: qsTr("Stroke position")
                        onActivated: root.parameter(String(card.styleData.id), ["alignment"], keys[currentIndex])
                    }
                }
                RowLayout {
                    visible: card.parameters.mode !== undefined
                    Label { text: qsTr("Bevel"); color: theme.textMuted; Layout.fillWidth: true }
                    ToneComboBox {
                        model: [qsTr("Inner"), qsTr("Outer"), qsTr("Emboss")]
                        readonly property var keys: ["inner", "outer", "emboss"]
                        currentIndex: keys.indexOf(String(card.parameters.mode))
                        Accessible.name: qsTr("Bevel mode")
                        onActivated: root.parameter(String(card.styleData.id), ["mode"], keys[currentIndex])
                    }
                }
                ColorRow {
                    visible: card.parameters.fill !== undefined && card.parameters.fill.kind === "solid"
                    label: qsTr("Stroke color")
                    value: card.parameters.fill && card.parameters.fill.color || ({r: 1, g: 1, b: 1, a: 1})
                    onChoose: root.chooseColor(String(card.styleData.id), ["fill", "color"], value)
                }
                RowLayout {
                    visible: card.parameters.fill !== undefined
                    Label {
                        text: qsTr("Stroke fill")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeControl
                        Layout.fillWidth: true
                    }
                    ToneComboBox {
                        objectName: "layerStyleFillKind_" + String(card.styleData.id)
                        model: [qsTr("Solid"), qsTr("Gradient")]
                        readonly property var keys: ["solid", "gradient"]
                        currentIndex: keys.indexOf(String(card.parameters.fill && card.parameters.fill.kind || "solid"))
                        Accessible.name: qsTr("Stroke fill type")
                        onActivated: root.setStrokeFillKind(String(card.styleData.id), keys[currentIndex])
                    }
                }
                ColumnLayout {
                    id: gradientControls
                    readonly property var gradient: card.parameters.gradient || (card.parameters.fill && card.parameters.fill.gradient) || ({})
                    readonly property var prefix: card.parameters.gradient ? ["gradient"] : ["fill", "gradient"]
                    visible: gradient.stops !== undefined
                    Layout.fillWidth: true
                    NumberRow {
                        label: qsTr("Gradient angle")
                        number: Number(gradientControls.gradient.angleDegrees || 0)
                        minimum: -360
                        maximum: 360
                        unit: qsTr("degrees")
                        onEdited: number => root.parameter(String(card.styleData.id), gradientControls.prefix.concat(["angleDegrees"]), number)
                    }
                    NumberRow {
                        label: qsTr("Gradient scale")
                        number: Number(gradientControls.gradient.scale || 1) * 100
                        minimum: 1
                        maximum: 1000
                        unit: "%"
                        onEdited: number => root.parameter(String(card.styleData.id), gradientControls.prefix.concat(["scale"]), number / 100)
                    }
                    ToneCheckBox {
                        text: qsTr("Reverse gradient")
                        checked: gradientControls.gradient.reverse === true
                        onToggled: root.parameter(String(card.styleData.id), gradientControls.prefix.concat(["reverse"]), checked)
                    }
                    Repeater {
                        model: gradientControls.gradient.stops || []
                        delegate: ColumnLayout {
                            id: stopEditor
                            required property int index
                            required property var modelData
                            readonly property var stops: gradientControls.gradient.stops || []
                            readonly property real positionPercent:
                                Number(stopEditor.modelData.position) * 100
                            readonly property real previousPercent: stopEditor.index > 0
                                ? Number(stopEditor.stops[stopEditor.index - 1].position) * 100 : 0
                            readonly property real nextPercent:
                                stopEditor.index < stopEditor.stops.length - 1
                                ? Number(stopEditor.stops[stopEditor.index + 1].position) * 100 : 100
                            readonly property real positionMinimum: stopEditor.index > 0
                                ? Math.min(stopEditor.positionPercent, stopEditor.previousPercent + 0.1) : 0
                            readonly property real positionMaximum:
                                stopEditor.index < stopEditor.stops.length - 1
                                ? Math.max(stopEditor.positionPercent, stopEditor.nextPercent - 0.1) : 100
                            Layout.fillWidth: true
                            spacing: theme.space2
                            ColorRow {
                                objectName: "layerStyleGradientStopColor_" +
                                    String(card.styleData.id) + "_" + stopEditor.index
                                label: qsTr("Stop %1").arg(stopEditor.index + 1)
                                value: stopEditor.modelData.color
                                onChoose: root.chooseColor(String(card.styleData.id),
                                    gradientControls.prefix.concat(["stops", stopEditor.index, "color"]),
                                    value, Number(stopEditor.modelData.position))
                            }
                            NumberRow {
                                objectName: "layerStyleGradientStopPosition_" +
                                    String(card.styleData.id) + "_" + stopEditor.index
                                label: qsTr("Stop %1 position").arg(stopEditor.index + 1)
                                number: stopEditor.positionPercent
                                minimum: stopEditor.positionMinimum
                                maximum: stopEditor.positionMaximum
                                unit: "%"
                                enabled: !root.locked && stopEditor.index > 0 &&
                                    stopEditor.index < stopEditor.stops.length - 1 &&
                                    stopEditor.nextPercent - stopEditor.previousPercent > 0.2
                                onEdited: number => root.parameter(String(card.styleData.id),
                                    gradientControls.prefix.concat(["stops", stopEditor.index, "position"]),
                                    number / 100)
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: theme.space4
                                MiniButton {
                                    objectName: "layerStyleGradientRemoveStop_" +
                                        String(card.styleData.id) + "_" + stopEditor.index
                                    text: qsTr("Remove stop")
                                    visible: stopEditor.index > 0 && stopEditor.index < stopEditor.stops.length - 1
                                    enabled: !root.locked && visible
                                    onClicked: root.removeGradientStop(String(card.styleData.id),
                                        gradientControls.prefix, stopEditor.index)
                                }
                            }
                        }
                    }
                    MiniButton {
                        objectName: "layerStyleGradientAddStop_" + String(card.styleData.id)
                        text: qsTr("Add stop")
                        enabled: !root.locked && root.canInsertGradientStop(gradientControls.gradient)
                        disabledReason: gradientControls.gradient.stops &&
                            gradientControls.gradient.stops.length >= 32
                            ? qsTr("A style gradient can contain at most 32 stops.")
                            : qsTr("There is no room for another stop. Move an interior stop first.")
                        onClicked: root.addGradientStop(String(card.styleData.id), gradientControls.prefix)
                    }
                }
                ColumnLayout {
                    visible: card.supported
                    Layout.fillWidth: true
                    spacing: theme.space2
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: card.styleData.maskRef
                                ? qsTr("Style mask attached") : qsTr("No style mask")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeControl
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                        MiniButton {
                            objectName: "layerStyleEditMask_" + String(card.styleData.id)
                            text: qsTr("Edit mask")
                            visible: String(card.styleData.maskRef || "").length > 0
                            enabled: !root.locked && visible
                            onClicked: {
                                root.requestStyleMaskEdit(String(card.styleData.id),
                                    String(card.styleData.maskRef))
                            }
                        }
                        MiniButton {
                            objectName: "layerStyleClearMask_" + String(card.styleData.id)
                            text: qsTr("Clear mask")
                            visible: String(card.styleData.maskRef || "").length > 0
                            enabled: !root.locked && visible
                            onClicked: {
                                const oldMaskId = String(card.styleData.maskRef || "")
                                const ok = root.controller.clearPhotoLayerStyleMask(
                                    root.layerIndex, String(card.styleData.id))
                                root.changed(ok)
                                if (ok) root.styleMaskClearRequested(root.layerIndex,
                                    String(card.styleData.id), oldMaskId)
                            }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: theme.space4
                        MiniButton {
                            objectName: "layerStyleMaskFromSelection_" + String(card.styleData.id)
                            text: String(card.styleData.maskRef || "").length > 0
                                ? qsTr("Replace from selection") : qsTr("Mask from selection")
                            enabled: !root.locked && root.controller.hasWorkingSelection
                            disabledReason: root.controller.hasWorkingSelection
                                ? qsTr("Unlock this layer and its parent groups to change its style mask.")
                                : qsTr("Create or activate a selection first.")
                            onClicked: {
                                const ok = root.controller.maskPhotoLayerStyleFromSelection(
                                    root.layerIndex, String(card.styleData.id))
                                root.changed(ok)
                                if (ok) {
                                    const updated = root.operation(String(card.styleData.id))
                                    root.requestStyleMaskEdit(String(card.styleData.id),
                                        String(updated.maskRef || ""))
                                }
                            }
                        }
                    }
                    Label {
                        visible: String(card.styleData.maskRef || "").length === 0 &&
                            !root.controller.hasWorkingSelection
                        text: qsTr("Create a selection with a selection tool, then use Mask from selection to give this style independent coverage.")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        Layout.fillWidth: true
                    }
                }
            }
            Label {
                visible: card.expanded && !card.supported
                text: qsTr("This style needs a newer renderer. Its settings are preserved; you can disable or remove it.")
                wrapMode: Text.WordWrap
                color: theme.textMuted
                Layout.fillWidth: true
            }
        }
    }
    Label {
        visible: root.status.length > 0
        text: root.status
        wrapMode: Text.WordWrap
        color: theme.textMuted
        Layout.fillWidth: true
    }
    ColorDialog {
        id: colorDialog
        title: qsTr("Style color")
        options: ColorDialog.ShowAlphaChannel
        onAccepted: {
            if (!root.colorTargetStillMatches()) {
                root.status = qsTr("The selected color target changed while the dialog was open. Reopen it to choose a color.")
                return
            }
            root.parameter(root.colorStyleId, root.colorPath, {r: root.linear(selectedColor.r),
                g: root.linear(selectedColor.g), b: root.linear(selectedColor.b), a: selectedColor.a})
            root.colorTargetSnapshot = null
            root.colorStopPosition = -1
        }
    }
    FileDialog {
        id: importDialog
        title: qsTr("Load layer style preset")
        nameFilters: [qsTr("Looksy styles (*.looksy-style)"), qsTr("All files (*)")]
        onAccepted: {
            if (root.dialogLayerId === root.layerId && !root.locked)
                root.changed(root.controller.importPhotoLayerStyleFile(root.layerIndex, selectedFile.toString()))
        }
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Save layer style preset")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "looksy-style"
        nameFilters: [qsTr("Looksy styles (*.looksy-style)")]
        onAccepted: {
            if (root.dialogLayerId === root.layerId)
                root.changed(root.controller.exportPhotoLayerStyleFile(root.layerIndex, selectedFile.toString()))
        }
    }
}
