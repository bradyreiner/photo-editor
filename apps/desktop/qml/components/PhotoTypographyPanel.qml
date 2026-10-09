pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import "." as Components

ColumnLayout {
    id: root

    required property var controller
    required property int layerIndex
    required property var layerData
    property var selectedLayerIndices: []
    property bool locked: false
    property bool expanded: true
    property bool showHeader: true
    property bool spacingExpanded: false
    property bool effectsExpanded: false
    property bool pathExpanded: false
    property bool axesExpanded: false
    property bool stylePresetsExpanded: false
    property bool baselineEditing: false
    property int selectedPresetIndex: 0
    property string presetFontNotice: ""
    readonly property bool upgraded: !!root.layerData.textTypography
    readonly property bool multiTarget: root.selectedLayerIndices && root.selectedLayerIndices.length > 1
    readonly property string targetId: String(root.layerData.id || "")
    readonly property string alignment: String(root.layerData.textAlignment || "left")
    readonly property var textMaterialOptions: [
        { id: "none", label: qsTr("No material") },
        { id: "frosted_glass", label: qsTr("Frosted glass"), tint: "#ff9eb8c8", blur: 6, transmission: 0.68, edge: 1, depth: 4, angle: -45 },
        { id: "smoked_plastic", label: qsTr("Smoked plastic"), tint: "#ffbac8d0", blur: 3, transmission: 0.45, edge: 0.6, depth: 6, angle: -60 },
        { id: "soft_emboss", label: qsTr("Soft emboss"), tint: "#ffc8bca8", blur: 1, transmission: 0.62, edge: 1.6, depth: 5, angle: -35 },
        { id: "polished_chrome", label: qsTr("Polished chrome"), tint: "#ffe8edf2", reflectivity: 0.94, edge: 1.1, depth: 8, angle: -45 },
        { id: "brushed_silver", label: qsTr("Brushed silver"), tint: "#ffc5cbd1", reflectivity: 0.78, edge: 1.5, depth: 5, angle: -55 },
        { id: "warm_gold", label: qsTr("Warm gold"), tint: "#ffdba33b", reflectivity: 0.88, edge: 1.1, depth: 7, angle: -48 },
        { id: "copper", label: qsTr("Polished copper"), tint: "#ffc1784e", reflectivity: 0.82, edge: 1.2, depth: 6, angle: -52 },
        { id: "dark_steel", label: qsTr("Dark steel"), tint: "#ff49525c", reflectivity: 0.82, edge: 1.3, depth: 7, angle: -42 },
        { id: "champagne_gold", label: qsTr("Champagne gold"), tint: "#ffcfb47a", reflectivity: 0.8, edge: 1.2, depth: 6, angle: -48 },
        { id: "rose_gold", label: qsTr("Rose gold"), tint: "#ffcf9286", reflectivity: 0.82, edge: 1.2, depth: 6, angle: -48 },
        { id: "antique_brass", label: qsTr("Antique brass"), tint: "#ffa88445", reflectivity: 0.7, edge: 1.8, depth: 8, angle: -40 },
        { id: "titanium", label: qsTr("Satin titanium"), tint: "#ff88989f", reflectivity: 0.74, edge: 1.5, depth: 5, angle: -54 },
        { id: "black_chrome", label: qsTr("Black chrome"), tint: "#ff343c49", reflectivity: 0.94, edge: 1.2, depth: 7, angle: -45 }
    ]
    readonly property var activeTextMaterial: root.textMaterialOptions.find(option =>
        option.id === String(root.style.material || "none")) || root.textMaterialOptions[0]
    readonly property bool isMetalMaterial: root.textMaterialOptions.some(option =>
        option.reflectivity !== undefined && option.id === String(root.style.material || "none"))
    readonly property var stylePresets: [
        { name: qsTr("Quiet Editorial"), families: ["Cormorant Garamond", "Fraunces", "Georgia"], weight: 400, tracking: 0.2, lineHeight: 1.16, color: "#fff4e9dc", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 2, shadowColor: "#70000000", material: "none" },
        { name: qsTr("Modern Magazine"), families: ["DM Sans", "Outfit", "Arial"], weight: 500, tracking: 0.4, lineHeight: 1.12, color: "#fff4f2ed", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 1, shadowColor: "#55000000", material: "none" },
        { name: qsTr("Soft Contrast Serif"), families: ["Fraunces", "Cormorant Garamond", "Georgia"], weight: 600, tracking: 0, lineHeight: 1.15, color: "#fff2ede4", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Gallery Italic"), families: ["Cormorant Garamond", "Fraunces", "Georgia"], weight: 500, tracking: 0.1, lineHeight: 1.2, color: "#fff8f4ec", italic: true, strokeWidth: 0, shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Warm Caption"), families: ["DM Sans", "Outfit", "Arial"], weight: 400, tracking: 1.1, lineHeight: 1.25, color: "#ffe7d4b2", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Swiss Grid"), families: ["Space Grotesk", "Outfit", "Arial"], weight: 600, tracking: -0.2, lineHeight: 1.08, color: "#fff7f7f2", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Geometric Bold"), families: ["Outfit", "Space Grotesk", "Arial"], weight: 700, tracking: -0.4, lineHeight: 1.02, color: "#fff8f6f2", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Mono Label"), families: ["Space Mono", "Space Grotesk", "Courier New"], weight: 400, tracking: 1.4, lineHeight: 1.2, color: "#ffb9d4c4", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Technical Mono"), families: ["Space Mono", "Space Grotesk", "Courier New"], weight: 700, tracking: 0.6, lineHeight: 1.1, color: "#ffb8d5ff", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Wide Geometry"), families: ["Space Grotesk", "Outfit", "Arial"], weight: 700, tracking: 1.5, lineHeight: 1.05, color: "#fff5f5f0", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Cinema Title"), families: ["Bebas Neue", "Archivo Black", "Arial"], weight: 700, tracking: 2.2, lineHeight: 0.98, color: "#fffff1d6", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 3, shadowColor: "#b0000000", material: "none" },
        { name: qsTr("Poster Condensed"), families: ["Bebas Neue", "Archivo Black", "Arial"], weight: 700, tracking: 0.7, lineHeight: 1, color: "#ffff5d48", italic: false, strokeWidth: 0, shadowX: 2, shadowY: 3, shadowColor: "#c0000000", material: "none" },
        { name: qsTr("Indie Zine"), families: ["Archivo Black", "Outfit", "Arial Black"], weight: 900, tracking: -0.5, lineHeight: 0.98, color: "#ffffd247", italic: false, strokeWidth: 0, shadowX: 3, shadowY: 3, shadowColor: "#b02d1745", material: "none" },
        { name: qsTr("Retro Display"), families: ["Fraunces", "Cormorant Garamond", "Georgia"], weight: 700, tracking: 0.8, lineHeight: 1.02, color: "#ffffb36a", italic: false, strokeWidth: 1, strokeColor: "#ff532b63", shadowX: 2, shadowY: 3, shadowColor: "#aa180a21", material: "none" },
        { name: qsTr("Newsprint"), families: ["Cormorant Garamond", "Fraunces", "Georgia"], weight: 700, tracking: 0.5, lineHeight: 1.08, color: "#fff2e3c2", italic: false, strokeWidth: 0, shadowX: 1, shadowY: 1, shadowColor: "#88000000", material: "none" },
        { name: qsTr("Outline Neon"), families: ["Space Grotesk", "Outfit", "Arial"], weight: 700, tracking: 1.2, lineHeight: 1.05, color: "#0015ffe1", italic: false, strokeWidth: 2, strokeColor: "#ff57ecff", shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Electric Sign"), families: ["Outfit", "Space Grotesk", "Arial"], weight: 700, tracking: 0.7, lineHeight: 1.05, color: "#ffff62cf", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 0, shadowColor: "#a0000000", material: "none" },
        { name: qsTr("Frosted Glass"), families: ["DM Sans", "Space Grotesk", "Arial"], weight: 600, tracking: 0.2, lineHeight: 1.1, color: "#ffeff8ff", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 1, shadowColor: "#66000000", material: "frosted_glass", materialTint: "#ff9eb8c8", materialBlur: 8, materialTransmission: 0.76, materialEdgeSoftness: 1.2, materialDepth: 2, materialLightAngle: -45 },
        { name: qsTr("Smoked Lettering"), families: ["Space Grotesk", "DM Sans", "Arial"], weight: 700, tracking: 0.8, lineHeight: 1.05, color: "#ffd9e7ee", italic: false, strokeWidth: 0, shadowX: 1, shadowY: 2, shadowColor: "#88000000", material: "smoked_plastic", materialTint: "#ffbac8d0", materialBlur: 3, materialTransmission: 0.45, materialEdgeSoftness: 0.6, materialDepth: 6, materialLightAngle: -60 },
        { name: qsTr("Soft Emboss"), families: ["Fraunces", "Cormorant Garamond", "Georgia"], weight: 700, tracking: 0.3, lineHeight: 1.1, color: "#ffe9dbc5", italic: false, strokeWidth: 0, shadowX: 0, shadowY: 1, shadowColor: "#66000000", material: "soft_emboss", materialTint: "#ffc8bca8", materialBlur: 1, materialTransmission: 0.62, materialEdgeSoftness: 1.6, materialDepth: 5, materialLightAngle: -35 },
        { name: qsTr("Soft Italic Mood"), families: ["Fraunces", "Cormorant Garamond", "Georgia"], weight: 500, tracking: 0.1, lineHeight: 1.18, color: "#ffffd2c0", italic: true, strokeWidth: 0, shadowX: 0, shadowY: 2, shadowColor: "#99000000", material: "none" }
    ]
    readonly property var style: {
        const revision = root.layerData
        return revision ? root.controller.photoLayerTypography(root.layerIndex) : ({})
    }
    readonly property var layoutInfo: {
        const revision = root.layerData
        return root.expanded && root.upgraded && revision
            ? root.controller.photoLayerTextLayoutInfo(root.layerIndex) : ({})
    }
    readonly property var variableAxes: root.upgraded && root.axesExpanded
        ? root.controller.availablePhotoTextAxes(String(root.style.family || ""), String(root.style.face || "")) : []
    signal baselineEditingRequested(bool active)

    function applyTypographyChanges(changes) {
        if (root.locked)
            return false
        return root.multiTarget
            ? root.controller.setPhotoLayersTypography(root.selectedLayerIndices, changes)
            : root.controller.setPhotoLayerTypography(root.layerIndex, changes)
    }

    function chooseFont(family) {
        if (root.locked || !family.length)
            return
        if (root.multiTarget)
            root.applyTypographyChanges({family: family, face: ""})
        else if (root.upgraded)
            root.applyTypographyChanges({family: family, face: "", axes: ({})})
        else
            root.applyLegacyStyle({family: family})
        root.presetFontNotice = ""
    }

    function applyLegacyStyle(changes) {
        const family = changes.family !== undefined ? String(changes.family) : String(root.style.family || "Helvetica")
        const size = changes.size !== undefined ? Number(changes.size) : Number(root.style.size || 48)
        let color = changes.color !== undefined ? String(changes.color) : String(root.style.color || "#ffffffff")
        const weight = changes.weight !== undefined ? Number(changes.weight) : Number(root.style.weight || 400)
        const normalizedColor = color.startsWith("#") ? color : "#" + color
        if (/^#[0-9a-fA-F]{8}$/.test(normalizedColor))
            color = normalizedColor
        if (root.multiTarget) {
            root.applyTypographyChanges({family: family, size: size, color: color,
                weight: weight >= 600 ? 700 : 400})
            return
        }
        root.controller.setPhotoLayerTextStyle(root.layerIndex, family, size, color,
            weight >= 600 ? "bold" : "regular", root.alignment)
    }

    function applyCoreStyle(changes) {
        if (root.locked)
            return
        if (root.multiTarget) {
            root.applyTypographyChanges(changes)
            return
        }
        if (changes.alignment !== undefined) {
            root.applyTypographyChanges({alignment: String(changes.alignment)})
            return
        }
        if (!root.upgraded && Object.keys(changes).every(key =>
                key === "family" || key === "size" || key === "color" || key === "weight")) {
            root.applyLegacyStyle(changes)
            return
        }
        root.applyTypographyChanges(changes)
    }

    function chooseTextMaterial(index) {
        if (root.locked || index < 0 || index >= root.textMaterialOptions.length)
            return
        const material = root.textMaterialOptions[index]
        if (material.id === "none") {
            root.applyCoreStyle({material: "none"})
            return
        }
        root.applyCoreStyle({
            material: material.id,
            materialTint: material.tint,
            materialBlur: material.blur ?? 0,
            materialTransmission: material.reflectivity ?? material.transmission,
            materialEdgeSoftness: material.edge,
            materialDepth: material.depth,
            materialLightAngle: material.angle
        })
    }

    function applyColor(color) {
        const value = String(color || "").trim()
        const normalized = value.startsWith("#") ? value : "#" + value
        if (/^#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(normalized)) {
            const explicitArgb = normalized.length === 7 ? "#ff" + normalized.slice(1) : normalized
            root.applyCoreStyle({color: explicitArgb})
        }
    }

    function colorChannelHex(value) {
        const channelByte = Math.max(0, Math.min(255, Math.round(Number(value) * 255)))
        const hex = channelByte.toString(16)
        return hex.length < 2 ? "0" + hex : hex
    }

    function colorToArgb(value) {
        return "#" + root.colorChannelHex(value.a) + root.colorChannelHex(value.r) +
               root.colorChannelHex(value.g) + root.colorChannelHex(value.b)
    }

    function resolvedFamilyForPreset(preset) {
        const installed = fontFamilyPicker.loadFamilies()
        for (let candidate of preset.families || []) {
            const match = installed.find(family => String(family).toLowerCase() === String(candidate).toLowerCase())
            if (match) return String(match)
        }
        const current = String(root.style.family || "")
        const currentMatch = installed.find(family => String(family).toLowerCase() === current.toLowerCase())
        if (currentMatch) return String(currentMatch)
        const resolved = String(root.layoutInfo.resolvedFamily || "")
        const resolvedMatch = installed.find(family => String(family).toLowerCase() === resolved.toLowerCase())
        if (resolvedMatch) return String(resolvedMatch)
        return installed.length ? String(installed[0]) : ""
    }

    function applyStylePreset() {
        if (root.locked || root.selectedPresetIndex < 0 ||
                root.selectedPresetIndex >= root.stylePresets.length) return
        const preset = root.stylePresets[root.selectedPresetIndex]
        const family = root.resolvedFamilyForPreset(preset)
        if (!family) {
            root.presetFontNotice = qsTr("No installed font is available for this style.")
            return
        }
        const preferred = String((preset.families || [])[0] || "")
        root.presetFontNotice = family.toLowerCase() === preferred.toLowerCase()
            ? qsTr("Using %1").arg(family)
            : qsTr("Using installed fallback: %1").arg(family)
        const changes = {
            family: family,
            face: "",
            weight: preset.weight,
            italic: preset.italic,
            underline: false,
            tracking: preset.tracking,
            lineHeight: preset.lineHeight,
            color: preset.color,
            strokeWidth: preset.strokeWidth,
            strokeColor: preset.strokeColor || "#ff000000",
            shadowX: preset.shadowX,
            shadowY: preset.shadowY,
            shadowColor: preset.shadowColor,
            material: preset.material,
            materialTint: preset.materialTint || "#ff9eb8c8",
            materialBlur: preset.materialBlur ?? 6,
            materialTransmission: preset.materialTransmission ?? 0.68,
            materialEdgeSoftness: preset.materialEdgeSoftness ?? 1,
            materialDepth: preset.materialDepth ?? 4,
            materialLightAngle: preset.materialLightAngle ?? -45
        }
        if (!root.multiTarget)
            changes.axes = ({})
        root.applyTypographyChanges(changes)
    }

    Components.Theme { id: theme }
    spacing: theme.space4
    onTargetIdChanged: {
        expanded = true
        spacingExpanded = false
        effectsExpanded = false
        pathExpanded = false
        axesExpanded = false
        presetFontNotice = ""
    }
    onStyleChanged: {
        if (!root.style.pathPoints || root.style.pathPoints.length < 2) {
            baselineEditing = false
            baselineEditingRequested(false)
        }
    }

    Components.MiniButton {
        objectName: "photoTypographyPanelHeader"
        visible: root.showHeader
        Layout.fillWidth: true
        text: qsTr("Typography")
        iconName: root.expanded ? "chevron-down" : "chevron-right"
        role: "ghost"
        active: root.expanded
        tooltip: qsTr("Edit the selected text layer's type, spacing, effects, and path.")
        onClicked: root.expanded = !root.expanded
    }

    ColumnLayout {
        objectName: "photoTypographyPanelContent"
        Layout.fillWidth: true
        visible: root.expanded
        spacing: theme.space4

        Label {
            objectName: "photoTypographyLockedNotice"
            Layout.fillWidth: true
            visible: root.locked
            wrapMode: Text.WordWrap
            text: qsTr("This text layer is locked. Unlock it in Layers to edit its typography.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }
        Label {
            Layout.fillWidth: true
            visible: root.upgraded && (!!root.layoutInfo.error || !!root.layoutInfo.overflow)
            wrapMode: Text.WordWrap
            text: root.layoutInfo.error ? String(root.layoutInfo.error)
                : qsTr("Text extends outside its box. Increase the box size or reduce the text size or spacing.")
            Accessible.name: text
            color: theme.warning
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }
        Label {
            objectName: "photoMissingFontWarning"
            Layout.fillWidth: true
            visible: root.upgraded
                ? (!!root.layoutInfo.missingFont || !!root.layoutInfo.missingFace)
                : root.controller.photoTextFontWarning(String(root.style.family || "Helvetica")).length > 0
            wrapMode: Text.WordWrap
            text: !root.upgraded
                ? root.controller.photoTextFontWarning(String(root.style.family || "Helvetica"))
                : root.layoutInfo.missingFont
                    ? qsTr("%1 is unavailable. Displaying %2; the saved font choice is preserved.").arg(root.style.family).arg(root.layoutInfo.resolvedFamily)
                    : qsTr("The saved font face is unavailable. Choose an installed face to replace it.")
            color: theme.warning
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }

        Components.PhotoFontPicker {
            id: fontFamilyPicker
            objectName: "photoTypographyFamilyPicker"
            triggerObjectName: "photoLayerTextFontField"
            controller: root.controller
            currentFamily: String(root.style.family || "Helvetica")
            enabled: !root.locked
            Layout.fillWidth: true
            Layout.preferredHeight: theme.controlHeightDense
            onFamilySelected: function(family) { root.chooseFont(family) }
        }

        Label {
            objectName: "photoTypographyMultiTargetNotice"
            Layout.fillWidth: true
            visible: root.multiTarget
            text: qsTr("%1 text layers selected · type changes apply to all").arg(root.selectedLayerIndices.length)
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
            Accessible.name: text
        }

        RowLayout {
            objectName: "photoTypographyFaceAndSizeRow"
            Layout.fillWidth: true
            spacing: theme.space4
            Components.ToneComboBox {
                objectName: "photoTypographyFace"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: theme.controlHeightDense
                visible: !root.multiTarget
                enabled: !root.locked
                readonly property var faces: root.controller.availablePhotoTextFaces(String(root.style.family || ""))
                model: [qsTr("Default face")].concat(faces)
                currentIndex: Math.max(0, faces.indexOf(String(root.style.face || "")) + 1)
                Accessible.name: qsTr("Font face")
                onActivated: function(index) {
                    root.applyTypographyChanges({
                        face: index === 0 ? "" : faces[index - 1],
                        axes: ({})
                    })
                }
            }
            Components.ToneTextField {
                id: sizeField
                objectName: "photoTypography_size"
                Layout.preferredWidth: theme.scaled(72)
                Layout.preferredHeight: theme.controlHeightDense
                enabled: !root.locked
                text: Number(root.style.size || 48).toFixed(2).replace(/\.?0+$/, "")
                Accessible.name: qsTr("Text size in pixels")
                selectByMouse: true
                validator: DoubleValidator {
                    bottom: 0.25
                    top: 8192
                    decimals: 2
                    notation: DoubleValidator.StandardNotation
                    locale: "C"
                }
                function restore() {
                    text = Qt.binding(() => Number(root.style.size || 48).toFixed(2).replace(/\.?0+$/, ""))
                }
                onEditingFinished: {
                    if (acceptableInput)
                        root.applyCoreStyle({size: Number(text)})
                    restore()
                }
                Keys.onEscapePressed: function(event) {
                    restore()
                    focus = false
                    event.accepted = true
                }
            }
            Label {
                text: qsTr("px")
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: theme.typeCaption
            }
        }

        RowLayout {
            objectName: "photoTypographyWeightColorRow"
            Layout.fillWidth: true
            spacing: theme.space4
            Components.MiniButton {
                objectName: "photoLayerTextBoldButton"
                Layout.preferredWidth: theme.scaled(38)
                text: "B"
                role: "segmented"
                active: Number(root.style.weight || 400) >= 600
                enabled: !root.locked
                Accessible.name: qsTr("Bold")
                tooltip: active ? qsTr("Use regular weight.") : qsTr("Use bold weight.")
                onClicked: root.applyCoreStyle(root.upgraded
                    ? {weight: active ? 400 : 700, face: ""}
                    : {weight: active ? 400 : 700})
            }
            Components.MiniButton {
                objectName: "photoTypographyItalicButton"
                Layout.preferredWidth: theme.scaled(32)
                visible: root.upgraded
                text: "I"
                role: "segmented"
                active: !!root.style.italic
                enabled: !root.locked
                Accessible.name: qsTr("Italic")
                tooltip: active ? qsTr("Turn italic off.") : qsTr("Turn italic on.")
                onClicked: root.applyCoreStyle({italic: !root.style.italic, face: ""})
            }
            Components.MiniButton {
                objectName: "photoTypographyUnderlineButton"
                Layout.preferredWidth: theme.scaled(32)
                visible: root.upgraded
                text: "U"
                role: "segmented"
                active: !!root.style.underline
                enabled: !root.locked
                Accessible.name: qsTr("Underline")
                tooltip: active ? qsTr("Remove underline.") : qsTr("Underline text.")
                onClicked: root.applyCoreStyle({underline: !root.style.underline})
            }
            Components.ColorSwatchButton {
                objectName: "photoTypographyFillColorSwatch"
                swatchColor: String(root.style.color || "#ffffffff")
                label: qsTr("Text fill: %1").arg(String(root.style.color || "#ffffffff"))
                enabled: !root.locked
                onPicked: fillColorDialog.open()
            }
            Components.ToneTextField {
                id: fillColorField
                objectName: "photoLayerTextColorField"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: theme.controlHeightDense
                enabled: !root.locked
                text: String(root.style.color || "#ffffffff")
                placeholderText: qsTr("#RRGGBB or #AARRGGBB")
                Accessible.name: qsTr("Text fill color")
                selectByMouse: true
                validator: RegularExpressionValidator {
                    regularExpression: /^#?(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
                }
                onEditingFinished: {
                    if (acceptableInput)
                        root.applyColor(text)
                    text = Qt.binding(() => String(root.style.color || "#ffffffff"))
                }
            }
        }

        RowLayout {
            objectName: "photoTypographyAlignmentRow"
            Layout.fillWidth: true
            spacing: theme.space2
            Repeater {
                model: [
                    { label: qsTr("Left"), value: "left" },
                    { label: qsTr("Center"), value: "center" },
                    { label: qsTr("Right"), value: "right" },
                    { label: qsTr("Justify"), value: "justify" }
                ]
                delegate: Components.MiniButton {
                    required property var modelData
                    objectName: "photoLayerTextAlign" + modelData.label + "Button"
                    Layout.fillWidth: true
                    text: modelData.label
                    role: "segmented"
                    active: root.alignment === modelData.value
                    enabled: !root.locked
                    tooltip: qsTr("Align text %1.").arg(String(modelData.value))
                    onClicked: root.applyCoreStyle({alignment: modelData.value})
                }
            }
        }

        Label {
            objectName: "photoTypographyLegacyNotice"
            Layout.fillWidth: true
            visible: !root.upgraded
            wrapMode: Text.WordWrap
            text: qsTr("Advanced type controls are available when you enable full typography. Existing text can reflow; the change is undoable.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
        }
        Components.MiniButton {
            objectName: "photoTypographyEnableAdvanced"
            Layout.fillWidth: true
            visible: !root.upgraded
            enabled: !root.locked
            text: qsTr("Enable advanced typography")
            iconName: "text-tool"
            role: "secondary"
            onClicked: root.applyCoreStyle({size: Number(root.style.size || 48)})
        }

        Components.MiniButton {
            objectName: "photoTypographyStylePresetToggle"
            Layout.fillWidth: true
            visible: root.upgraded
            text: qsTr("Style presets")
            iconName: "sparkles"
            role: "ghost"
            tooltip: qsTr("Choose a starting point, then customize the style.")
            onClicked: root.stylePresetsExpanded = !root.stylePresetsExpanded
        }
        RowLayout {
            id: stylePresetPanel
            objectName: "photoTypographyStylePresetPicker"
            Layout.fillWidth: true
            visible: root.upgraded && root.stylePresetsExpanded
            spacing: theme.space4
            Components.ToneComboBox {
                objectName: "photoTypographyStylePreset"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: theme.controlHeightDense
                enabled: !root.locked
                model: root.stylePresets.map(preset => preset.name)
                currentIndex: root.selectedPresetIndex
                Accessible.name: qsTr("Typography style preset")
                Accessible.description: qsTr("Choose a complete font style for this text layer.")
                onActivated: function(index) { root.selectedPresetIndex = index }
            }
            Components.MiniButton {
                objectName: "photoTypographyStylePresetApply"
                Layout.preferredWidth: theme.scaled(72)
                text: qsTr("Apply")
                role: "primary"
                enabled: !root.locked
                Accessible.name: qsTr("Apply typography style preset")
                tooltip: qsTr("Apply the selected font, color, spacing, and effects to this text.")
                onClicked: root.applyStylePreset()
            }
        }
        Label {
            objectName: "photoTypographyStylePresetFontStatus"
            Layout.fillWidth: true
            visible: root.upgraded && root.presetFontNotice.length > 0
            text: root.presetFontNotice
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
            Accessible.name: text
        }

        Components.MiniButton {
            objectName: "photoTypographySpacingToggle"
            Layout.fillWidth: true
            visible: root.upgraded
            text: qsTr("Spacing and layout")
            iconName: root.spacingExpanded ? "chevron-down" : "chevron-right"
            role: "ghost"
            active: root.spacingExpanded
            onClicked: root.spacingExpanded = !root.spacingExpanded
        }
        ColumnLayout {
            objectName: "photoTypographySpacingControls"
            Layout.fillWidth: true
            visible: root.upgraded && root.spacingExpanded
            spacing: theme.space4
            RowLayout {
                Layout.fillWidth: true
                visible: !root.multiTarget
                Components.ToneComboBox {
                    objectName: "photoTextModeControl"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: theme.controlHeightDense
                    enabled: !root.locked
                    model: [qsTr("Paragraph box"), qsTr("Point text · auto size")]
                    currentIndex: root.style.mode === "point" ? 1 : 0
                    Accessible.name: qsTr("Text layout mode")
                    onActivated: function(index) {
                        root.controller.setPhotoLayerTypography(root.layerIndex, {mode: index === 1 ? "point" : "paragraph"})
                    }
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: theme.space4
                rowSpacing: theme.space4
                Repeater {
                    model: [
                        { key: "tracking", label: qsTr("Letter spacing · px"), minimum: -8192, maximum: 8192 },
                        { key: "lineHeight", label: qsTr("Line height · × size"), minimum: 0.1, maximum: 20 },
                        { key: "paragraphSpacing", label: qsTr("Paragraph spacing · px"), minimum: 0, maximum: 8192 },
                        { key: "baselineOffset", label: qsTr("Baseline shift · px"), minimum: -8192, maximum: 8192 }
                    ]
                    delegate: ColumnLayout {
                        id: spacingField
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: theme.space2
                        Label {
                            Layout.fillWidth: true
                            text: spacingField.modelData.label
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                        Components.ToneTextField {
                            objectName: "photoTypography_" + spacingField.modelData.key
                            Layout.fillWidth: true
                            Layout.preferredHeight: theme.controlHeightDense
                            enabled: !root.locked
                            text: String(root.style[spacingField.modelData.key] ?? 0)
                            Accessible.name: spacingField.modelData.label
                            selectByMouse: true
                            validator: DoubleValidator {
                                bottom: spacingField.modelData.minimum
                                top: spacingField.modelData.maximum
                                decimals: 4
                                notation: DoubleValidator.StandardNotation
                                locale: "C"
                            }
                            onEditingFinished: {
                                if (acceptableInput) {
                                    const change = ({})
                                    change[spacingField.modelData.key] = Number(text)
                                    root.applyCoreStyle(change)
                                }
                                text = Qt.binding(() => String(root.style[spacingField.modelData.key] ?? 0))
                            }
                        }
                    }
                }
            }
            Components.MiniButton {
                objectName: "photoTypographyVariableAxesToggle"
                Layout.fillWidth: true
                visible: root.upgraded && !root.multiTarget
                text: qsTr("Variable font controls")
                iconName: root.axesExpanded ? "chevron-down" : "chevron-right"
                role: "ghost"
                active: root.axesExpanded
                onClicked: root.axesExpanded = !root.axesExpanded
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.upgraded && root.axesExpanded && !root.multiTarget
                spacing: theme.space4
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.variableAxes.length
                        ? qsTr("Available axes change with the selected font face.")
                        : qsTr("This font face has no adjustable variable axes.")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                }
                Repeater {
                    model: root.variableAxes
                    delegate: RowLayout {
                        id: variableAxisRow
                        required property var modelData
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: String(variableAxisRow.modelData.name || variableAxisRow.modelData.tag) +
                                  " · " + String(variableAxisRow.modelData.tag)
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            elide: Text.ElideRight
                        }
                        Components.ToneTextField {
                            objectName: "photoTypographyAxis_" + String(variableAxisRow.modelData.tag)
                            Layout.preferredWidth: 76
                            Layout.preferredHeight: theme.controlHeightDense
                            enabled: !root.locked
                            text: String((root.style.axes || ({}))[variableAxisRow.modelData.tag] ?? variableAxisRow.modelData.default)
                            Accessible.name: String(variableAxisRow.modelData.name || variableAxisRow.modelData.tag) + qsTr(" font axis value")
                            selectByMouse: true
                            validator: DoubleValidator {
                                bottom: variableAxisRow.modelData.minimum
                                top: variableAxisRow.modelData.maximum
                                decimals: 3
                                notation: DoubleValidator.StandardNotation
                                locale: "C"
                            }
                            onEditingFinished: {
                                if (acceptableInput) {
                                    const axes = Object.assign({}, root.style.axes || ({}))
                                    axes[variableAxisRow.modelData.tag] = Number(text)
                                    root.controller.setPhotoLayerTypography(root.layerIndex, {axes: axes})
                                }
                                text = Qt.binding(() => String((root.style.axes || ({}))[variableAxisRow.modelData.tag] ?? variableAxisRow.modelData.default))
                            }
                        }
                    }
                }
            }
        }

        Components.MiniButton {
            objectName: "photoTypographyEffectsToggle"
            Layout.fillWidth: true
            visible: root.upgraded
            text: qsTr("Stroke, shadow and materials")
            iconName: root.effectsExpanded ? "chevron-down" : "chevron-right"
            role: "ghost"
            active: root.effectsExpanded
            onClicked: root.effectsExpanded = !root.effectsExpanded
        }
        ColumnLayout {
            objectName: "photoTypographyEffectsControls"
            Layout.fillWidth: true
            visible: root.upgraded && root.effectsExpanded
            spacing: theme.space4
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: theme.space4
                rowSpacing: theme.space4
                Repeater {
                    model: [
                        { key: "strokeWidth", label: qsTr("Stroke · px"), minimum: 0, maximum: 256 },
                        { key: "shadowX", label: qsTr("Shadow X · px"), minimum: -8192, maximum: 8192 },
                        { key: "shadowY", label: qsTr("Shadow Y · px"), minimum: -8192, maximum: 8192 }
                    ]
                    delegate: ColumnLayout {
                        id: effectMetric
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: theme.space2
                        Label {
                            text: effectMetric.modelData.label
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                        Components.ToneTextField {
                            objectName: "photoTypography_" + effectMetric.modelData.key
                            Layout.fillWidth: true
                            Layout.preferredHeight: theme.controlHeightDense
                            enabled: !root.locked
                            text: String(root.style[effectMetric.modelData.key] || 0)
                            Accessible.name: effectMetric.modelData.label
                            selectByMouse: true
                            validator: DoubleValidator {
                                bottom: effectMetric.modelData.minimum
                                top: effectMetric.modelData.maximum
                                decimals: 3
                                notation: DoubleValidator.StandardNotation
                                locale: "C"
                            }
                            onEditingFinished: {
                                if (acceptableInput) {
                                    const change = ({})
                                    change[effectMetric.modelData.key] = Number(text)
                                    root.applyCoreStyle(change)
                                }
                                text = Qt.binding(() => String(root.style[effectMetric.modelData.key] || 0))
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: [
                        {key: "strokeColor", label: qsTr("Stroke color"), fallback: "#ff000000"},
                        {key: "shadowColor", label: qsTr("Shadow color"), fallback: "#a0000000"}
                    ]
                    delegate: ColumnLayout {
                        id: effectColor
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: theme.space2
                        Label {
                            text: effectColor.modelData.label
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                        Components.ToneTextField {
                            Layout.fillWidth: true
                            enabled: !root.locked
                            Accessible.name: effectColor.modelData.label
                            text: String(root.style[effectColor.modelData.key] || effectColor.modelData.fallback)
                            onEditingFinished: {
                                const change = ({})
                                change[effectColor.modelData.key] = text
                                root.applyCoreStyle(change)
                                text = Qt.binding(() => String(root.style[effectColor.modelData.key] || effectColor.modelData.fallback))
                            }
                        }
                    }
                }
            }
            Components.ToneComboBox {
                objectName: "photoTypographyMaterial"
                Layout.fillWidth: true
                enabled: !root.locked
                model: root.textMaterialOptions.map(option => option.label)
                currentIndex: Math.max(0, root.textMaterialOptions.findIndex(option => option.id === String(root.style.material || "none")))
                Accessible.name: qsTr("Text material")
                onActivated: function(index) { root.chooseTextMaterial(index) }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: String(root.style.material || "none") !== "none"
                Components.ToneTextField {
                    objectName: "photoTypographyMaterialTint"
                    Layout.fillWidth: true
                    enabled: !root.locked
                    Accessible.name: qsTr("Material tint color")
                    placeholderText: qsTr("Tint · #AARRGGBB")
                    text: String(root.style.materialTint || "#ff9eb8c8")
                    onEditingFinished: {
                        root.applyCoreStyle({materialTint: text})
                        text = Qt.binding(() => String(root.style.materialTint || "#ff9eb8c8"))
                    }
                }
                Components.MiniButton {
                    objectName: "photoTypographyMaterialTintReset"
                    enabled: !root.locked
                    iconName: "reset"
                    role: "icon"
                    Accessible.name: qsTr("Reset material tint")
                    tooltip: qsTr("Reset tint to this material's finish color.")
                    onClicked: root.applyCoreStyle({materialTint: root.activeTextMaterial.tint || "#ff9eb8c8"})
                }
            }
            Repeater {
                model: [
                    { key: "materialBlur", label: qsTr("Backdrop blur · px"), minimum: 0, maximum: 64, step: 0.5, defaultValue: root.activeTextMaterial.blur ?? 6, visible: String(root.style.material || "none") === "frosted_glass" },
                    { key: "materialTransmission", label: root.isMetalMaterial ? qsTr("Reflection") : qsTr("Transmission"), minimum: 0, maximum: 1, step: 0.01, defaultValue: root.activeTextMaterial.reflectivity ?? root.activeTextMaterial.transmission ?? 0.68, visible: root.isMetalMaterial || ["frosted_glass", "smoked_plastic"].indexOf(String(root.style.material || "none")) >= 0 },
                    { key: "materialEdgeSoftness", label: root.isMetalMaterial ? qsTr("Bevel softness") : qsTr("Edge softness"), minimum: 0, maximum: 16, step: 0.25, defaultValue: root.activeTextMaterial.edge ?? 1 },
                    { key: "materialDepth", label: root.isMetalMaterial ? qsTr("Bevel depth") : qsTr("Relief depth"), minimum: 0, maximum: 32, step: 0.25, defaultValue: root.activeTextMaterial.depth ?? 4 },
                    { key: "materialLightAngle", label: qsTr("Light angle · °"), minimum: -180, maximum: 180, step: 1, defaultValue: root.activeTextMaterial.angle ?? -45 }
                ]
                delegate: Components.InspectorSlider {
                    id: materialField
                    objectName: "photoTypography_" + modelData.key
                    required property var modelData
                    visible: String(root.style.material || "none") !== "none" && Boolean(modelData.visible !== false)
                    Layout.fillWidth: true
                    controlHeight: theme.controlHeightDense
                    forceCompact: true
                    enabled: !root.locked
                    label: modelData.label
                    from: modelData.minimum
                    to: modelData.maximum
                    stepSize: modelData.step
                    value: Number(root.style[modelData.key] ?? 0)
                    valueText: Number(value).toFixed(modelData.step < 1 ? 2 : 0)
                    defaultValue: modelData.defaultValue
                    onInteractionStarted: root.controller.beginCoalescedEdit()
                    onInteractionFinished: root.controller.endCoalescedEdit()
                    onValueDragged: function(nextValue) {
                        const change = ({})
                        change[modelData.key] = nextValue
                        root.applyCoreStyle(change)
                    }
                    onResetRequested: valueDragged(defaultValue)
                }
            }
        }

        Components.MiniButton {
            objectName: "photoTypographyPathToggle"
            Layout.fillWidth: true
            visible: root.upgraded && !root.multiTarget
            text: qsTr("Text on a selection path")
            iconName: root.pathExpanded ? "chevron-down" : "chevron-right"
            role: "ghost"
            active: root.pathExpanded
            onClicked: root.pathExpanded = !root.pathExpanded
        }
        ColumnLayout {
            objectName: "photoTypographyPathControls"
            Layout.fillWidth: true
            visible: root.upgraded && root.pathExpanded && !root.multiTarget
            spacing: theme.space4
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space4
                Components.MiniButton {
                    objectName: "photoTextPathFromSelection"
                    Layout.fillWidth: true
                    text: root.style.pathPoints && root.style.pathPoints.length
                        ? qsTr("Replace contour") : qsTr("Use selection")
                    iconName: "masks"
                    role: "secondary"
                    enabled: !root.locked
                    tooltip: qsTr("Detach the active selection outline as this text layer's editable baseline.")
                    onClicked: root.controller.setPhotoLayerTextPathFromActiveSelection(root.layerIndex, 0)
                }
                Components.MiniButton {
                    objectName: "photoTextPathHandleEditButton"
                    visible: !!root.style.pathPoints && root.style.pathPoints.length > 1
                    Layout.fillWidth: true
                    text: root.baselineEditing ? qsTr("Done baseline") : qsTr("Edit baseline")
                    iconName: root.baselineEditing ? "check" : "move"
                    role: root.baselineEditing ? "primary" : "secondary"
                    enabled: !root.locked
                    tooltip: qsTr("Drag the baseline points on the photo to refine this text path.")
                    onClicked: {
                        root.baselineEditing = !root.baselineEditing
                        root.baselineEditingRequested(root.baselineEditing)
                    }
                }
                Components.MiniButton {
                    visible: !!root.style.pathPoints && root.style.pathPoints.length > 1
                    enabled: !root.locked
                    iconName: "close"
                    role: "icon"
                    Accessible.name: qsTr("Clear text path")
                    tooltip: qsTr("Detach text from its contour and return to ordinary text layout.")
                    onClicked: root.controller.clearPhotoLayerTextPath(root.layerIndex)
                }
            }
            Label {
                Layout.fillWidth: true
                visible: !!root.style.pathPoints && root.style.pathPoints.length > 1
                wrapMode: Text.WordWrap
                text: root.layoutInfo.overflow
                    ? qsTr("Text exceeds this contour interval. Extend the end point, reduce spacing, or shorten the text.")
                    : qsTr("Baseline is detached from the selection and stays editable if the mask changes.")
                color: root.layoutInfo.overflow ? theme.warning : theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
            Label {
                objectName: "photoTextPathContourLegend"
                Layout.fillWidth: true
                visible: root.baselineEditing && !!root.style.pathContours && root.style.pathContours.length > 1
                wrapMode: Text.WordWrap
                text: qsTr("Teal dashed lines show the other captured contours and holes. Drag the accent handles to edit the main baseline.")
                color: "#62d6c4"
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                Accessible.name: text
            }
            Components.ToneCheckBox {
                objectName: "photoTextContourExclusionCheckBox"
                Layout.fillWidth: true
                visible: !!root.style.pathPoints && root.style.pathPoints.length > 2 && !!root.style.pathClosed
                enabled: !root.locked
                text: qsTr("Flow paragraph around contour")
                accessibleLabel: qsTr("Flow paragraph around contour")
                accessibleDescription: qsTr("Wrap editable paragraph text in the widest available space beside the closed selection contour.")
                checked: !!root.style.pathExclusion
                onToggled: root.controller.setPhotoLayerTypography(root.layerIndex, {pathExclusion: checked})
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !!root.style.pathPoints && root.style.pathPoints.length > 1
                spacing: theme.space4
                Components.ToneComboBox {
                    Layout.preferredWidth: 92
                    Layout.preferredHeight: theme.controlHeightDense
                    enabled: !root.locked
                    model: [qsTr("Left side"), qsTr("Right side")]
                    currentIndex: root.style.pathSide === "right" ? 1 : 0
                    Accessible.name: qsTr("Contour text side")
                    onActivated: function(index) {
                        root.controller.setPhotoLayerTypography(root.layerIndex, {pathSide: index === 1 ? "right" : "left"})
                    }
                }
                Components.ToolValueSlider {
                    Layout.fillWidth: true
                    Layout.preferredHeight: theme.controlHeightDense
                    enabled: !root.locked
                    from: 0; to: 1; stepSize: 0.01
                    value: Number(root.style.pathStart || 0)
                    label: qsTr("Start")
                    valueText: Math.round(value * 100) + "%"
                    onMoved: root.controller.setPhotoLayerTypography(root.layerIndex, {pathStart: value})
                }
                Components.ToolValueSlider {
                    Layout.fillWidth: true
                    Layout.preferredHeight: theme.controlHeightDense
                    enabled: !root.locked
                    from: 0.01; to: 1; stepSize: 0.01
                    value: Number(root.style.pathEnd || 1)
                    label: qsTr("End")
                    valueText: Math.round(value * 100) + "%"
                    onMoved: root.controller.setPhotoLayerTypography(root.layerIndex, {pathEnd: value})
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !!root.style.pathPoints && root.style.pathPoints.length > 1
                spacing: theme.space4
                Components.ToolValueSlider {
                    Layout.fillWidth: true
                    Layout.preferredHeight: theme.controlHeightDense
                    enabled: !root.locked
                    from: -128; to: 128; stepSize: 1
                    value: Number(root.style.pathOffset || 0)
                    label: qsTr("Offset")
                    valueText: Math.round(value) + " px"
                    onMoved: root.controller.setPhotoLayerTypography(root.layerIndex, {pathOffset: value})
                }
                Components.ToolValueSlider {
                    Layout.fillWidth: true
                    Layout.preferredHeight: theme.controlHeightDense
                    enabled: !root.locked
                    from: 0; to: 32; stepSize: 0.25
                    value: Number(root.style.pathSpacing || 0)
                    label: qsTr("Spacing")
                    valueText: value.toFixed(1) + " px"
                    onMoved: root.controller.setPhotoLayerTypography(root.layerIndex, {pathSpacing: value})
                }
            }
        }
    }

    ColorDialog {
        id: fillColorDialog
        title: qsTr("Text fill color")
        options: ColorDialog.ShowAlphaChannel
        selectedColor: root.style.color || "#ffffffff"
        onAccepted: root.applyColor(root.colorToArgb(selectedColor))
    }
}
