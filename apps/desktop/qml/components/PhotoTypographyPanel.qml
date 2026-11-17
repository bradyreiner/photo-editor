pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import "." as Components

ColumnLayout {
    id: root

    required property var controller
    required property int layerIndex
    required property var layerData
    property int textSelectionStart: -1
    property int textSelectionEnd: -1
    property string textSelectionContent: ""
    property var contentEditor: null
    property var draftSession: null
    property bool forceWholeLayerScope: false
    property var selectedLayerIndices: []
    property var selectedLayerIds: []
    property bool locked: false
    property bool expanded: true
    property bool showHeader: true
    property alias spacingExpanded: typographyPreferences.spacingExpanded
    property alias effectsExpanded: typographyPreferences.effectsExpanded
    property alias pathExpanded: typographyPreferences.pathExpanded
    property bool axesExpanded: false
    property alias stylePresetsExpanded: typographyPreferences.stylePresetsExpanded
    property alias favoritePresetsOnly: typographyPreferences.favoritePresetsOnly
    property bool baselineEditing: false
    property int selectedPresetIndex: 0
    property string presetFontNotice: ""
    readonly property bool upgraded: !!root.draftSession || !!root.layerData.textTypography
    readonly property bool multiTarget: root.selectedLayerIndices && root.selectedLayerIndices.length > 1
    readonly property string targetId: String(root.layerData.id || "")
    readonly property string alignment: String(root.layerData.textAlignment || "left")
    readonly property bool textSelectionAvailable: !root.multiTarget && root.textSelectionStart >= 0 &&
        root.textSelectionEnd > root.textSelectionStart && root.textSelectionEnd <= root.textSelectionContent.length
    readonly property bool textSelectionActive: root.textSelectionAvailable && !root.forceWholeLayerScope
    readonly property var favoritePresetNames: {
        try {
            const saved = JSON.parse(typographyPreferences.favoritePresetNames)
            return Array.isArray(saved) ? saved.map(name => String(name)) : []
        } catch (_) { return [] }
    }
    readonly property var visibleStylePresetIndices: {
        const favoriteNames = root.favoritePresetNames
        const all = root.stylePresets.map((preset, index) => index)
        return root.favoritePresetsOnly
            ? all.filter(index => favoriteNames.indexOf(String(root.stylePresets[index].name)) >= 0)
            : all
    }
    readonly property bool selectedPresetIsFavorite: root.selectedPresetIndex >= 0 &&
        root.selectedPresetIndex < root.stylePresets.length &&
        root.favoritePresetNames.indexOf(String(root.stylePresets[root.selectedPresetIndex].name)) >= 0
    property string editingEffectColorKey: ""
    property var colorEditContext: null
    property var activeColorDialog: null
    property var colorOpacityContext: null
    property real pendingColorOpacity: -1
    property string pendingDialogColor: ""
    readonly property color displayedFillColor: String(root.displayedStyle.color || "#ffffffff")
    readonly property var displayedStyle: {
        const base = root.style
        if (!root.textSelectionActive) return base
        const start = root.textSelectionStart
        const runs = base.runs || []
        for (let index = 0; index < runs.length; ++index) {
            const run = runs[index] || ({})
            const runStart = Number(run.start || 0)
            const runEnd = runStart + Number(run.length || 0)
            if (runStart <= start && runEnd > start) {
                const result = Object.assign({}, base, run)
                delete result.start
                delete result.length
                return result
            }
        }
        return base
    }
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
        return root.draftSession ? root.draftSession.style
            : revision ? root.controller.photoLayerTypography(root.layerIndex) : ({})
    }
    readonly property var layoutInfo: {
        const revision = root.layerData
        return !root.draftSession && root.expanded && root.upgraded && revision
            ? root.controller.photoLayerTextLayoutInfo(root.layerIndex) : ({})
    }
    readonly property var variableAxes: root.upgraded && root.axesExpanded
        ? root.controller.availablePhotoTextAxes(String(root.style.family || ""), String(root.style.face || "")) : []
    signal baselineEditingRequested(bool active)

    Settings {
        id: typographyPreferences
        category: "TypographyPanel"
        property string favoritePresetNames: "[]"
        property bool favoritePresetsOnly: false
        property bool stylePresetsExpanded: true
        property bool spacingExpanded: false
        property bool effectsExpanded: false
        property bool pathExpanded: false
    }

    function applyTypographyChanges(changes) {
        if (root.locked)
            return false
        if (root.draftSession) return root.draftSession.applyDraftTypography(changes)
        return root.multiTarget
            ? root.controller.setPhotoLayersTypography(root.selectedLayerIndices, changes)
            : root.controller.setPhotoLayerTypography(root.layerIndex, changes)
    }

    function applyCharacterStyle(changes) {
        if (root.locked || !root.textSelectionActive) return false
        if (root.contentEditor && typeof root.contentEditor.flushPendingText === "function")
            if (!root.contentEditor.flushPendingText()) return false
        const start = root.textSelectionStart
        const end = root.textSelectionEnd
        const content = String(root.textSelectionContent || "")
        if (start < 0 || end <= start || end > content.length) return false
        if (root.draftSession) return root.draftSession.applyDraftTypography(changes, start, end)
        return root.controller.setPhotoLayerTextRangeStyle(
            root.layerIndex, start, end, changes, content)
    }

    function toggleAdvancedSection(section) {
        const nextExpanded = section === "layout" ? !root.spacingExpanded
            : section === "finish" ? !root.effectsExpanded
            : section === "path" ? !root.pathExpanded : false
        root.spacingExpanded = section === "layout" && nextExpanded
        root.effectsExpanded = section === "finish" && nextExpanded
        root.pathExpanded = section === "path" && nextExpanded
    }

    function toggleSelectedPresetFavorite() {
        if (root.selectedPresetIndex < 0 || root.selectedPresetIndex >= root.stylePresets.length)
            return false
        const name = String(root.stylePresets[root.selectedPresetIndex].name)
        const next = root.favoritePresetNames.slice()
        const current = next.indexOf(name)
        if (current >= 0) next.splice(current, 1)
        else next.push(name)
        typographyPreferences.favoritePresetNames = JSON.stringify(next)
        if (current >= 0 && root.favoritePresetsOnly) {
            const remaining = root.visibleStylePresetIndices
            if (remaining.length > 0)
                root.selectedPresetIndex = remaining[0]
        }
        return true
    }

    function toggleFavoritePresetFilter() {
        root.favoritePresetsOnly = !root.favoritePresetsOnly
        const available = root.visibleStylePresetIndices
        if (available.length > 0 && available.indexOf(root.selectedPresetIndex) < 0)
            root.selectedPresetIndex = available[0]
    }

    function openEffectColorDialog(key) {
        return root.openColorDialog(effectColorDialog, String(key))
    }

    function effectColorValue(key) {
        const style = String(key) === "strokeColor" ? root.displayedStyle : root.style
        const fallback = String(key) === "strokeColor" ? "#ff000000" : "#a0000000"
        return String(style[String(key)] || fallback)
    }

    function chooseFont(family) {
        if (root.locked || !family.length)
            return
        if (root.textSelectionActive)
            root.applyCharacterStyle({family: family})
        else if (root.multiTarget)
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
        const characterKeys = ["family", "size", "weight", "italic", "underline", "color", "strokeWidth", "strokeColor"]
        if (root.textSelectionActive && Object.keys(changes).every(key => characterKeys.indexOf(key) >= 0)) {
            root.applyCharacterStyle(changes)
            return
        }
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

    function adjustTextSize(delta) {
        if (root.locked) return false
        const current = Number(root.displayedStyle.size || 48)
        const next = Math.max(0.25, Math.min(8192, current + Number(delta || 0)))
        if (!Number.isFinite(next) || next === current) return false
        root.applyCoreStyle({size: next})
        return true
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
            const context = root.prepareColorContext("color")
            return context ? root.applyColorToContext(explicitArgb, context) : false
        }
        return false
    }

    function prepareColorContext(key) {
        if (root.multiTarget && (!root.selectedLayerIds ||
                root.selectedLayerIds.length !== root.selectedLayerIndices.length)) return null
        if (root.textSelectionActive && root.contentEditor &&
                typeof root.contentEditor.flushPendingText === "function" &&
                !root.contentEditor.flushPendingText()) return null
        return root.captureColorContext(key)
    }

    function captureColorContext(key) {
        const indices = root.selectedLayerIndices ? root.selectedLayerIndices.slice() : []
        const selectedIds = root.selectedLayerIds ? root.selectedLayerIds.map(id => String(id)) : []
        const editor = root.contentEditor
        return {
            key: String(key || "color"),
            controller: root.controller,
            targetId: root.targetId,
            layerIndex: root.layerIndex,
            textSelectionActive: root.textSelectionActive,
            textSelectionStart: root.textSelectionStart,
            textSelectionEnd: root.textSelectionEnd,
            textSelectionContent: String(root.textSelectionContent || ""),
            contentEditor: root.contentEditor,
            draftSession: root.draftSession,
            outerTextSession: !!(editor && (editor.editing || editor.transactionOpen || editor.editTransactionOpen)),
            upgraded: root.upgraded,
            multiTarget: root.multiTarget,
            selectedLayerIndices: indices,
            selectedLayerIds: selectedIds,
            style: Object.assign({}, root.style),
            displayedStyle: Object.assign({}, root.displayedStyle),
            alignment: root.alignment,
            fillColor: root.displayedFillColor,
            transactionOpen: false,
            lastAppliedColor: ""
        }
    }

    function colorContextIsCurrent(context) {
        if (!context || root.locked || root.controller !== context.controller ||
                root.targetId !== context.targetId ||
                root.layerIndex !== context.layerIndex || root.draftSession !== context.draftSession ||
                root.contentEditor !== context.contentEditor ||
                root.multiTarget !== context.multiTarget ||
                JSON.stringify(root.selectedLayerIndices || []) !== JSON.stringify(context.selectedLayerIndices || []))
            return false
        const selectedIds = root.selectedLayerIds ? root.selectedLayerIds.map(id => String(id)) : []
        if (JSON.stringify(selectedIds) !== JSON.stringify(context.selectedLayerIds || [])) return false
        const editor = root.contentEditor
        if (!!(editor && (editor.editing || editor.transactionOpen || editor.editTransactionOpen)) !==
                context.outerTextSession) return false
        return root.textSelectionActive === context.textSelectionActive &&
            (!context.textSelectionActive ||
             (root.textSelectionStart === context.textSelectionStart &&
              root.textSelectionEnd === context.textSelectionEnd &&
              String(root.textSelectionContent || "") === context.textSelectionContent))
    }

    function applyColorToContext(color, context) {
        if (!root.colorContextIsCurrent(context)) return false
        const change = ({})
        change[context.key] = String(color)
        const characterStyle = context.key === "color" || context.key === "strokeColor"
        if (context.textSelectionActive && characterStyle) {
            if (context.contentEditor && typeof context.contentEditor.flushPendingText === "function" &&
                    !context.contentEditor.flushPendingText()) return false
            if (context.draftSession)
                return context.draftSession.applyDraftTypography(change,
                    context.textSelectionStart, context.textSelectionEnd)
        return context.controller.setPhotoLayerTextRangeStyle(context.layerIndex,
                context.textSelectionStart, context.textSelectionEnd, change,
                context.textSelectionContent)
        }
        if (context.draftSession)
            return context.draftSession.applyDraftTypography(change)
        if (context.multiTarget)
            return context.controller.setPhotoLayersTypography(context.selectedLayerIndices, change)
        if (context.upgraded)
            return context.controller.setPhotoLayerTypography(context.layerIndex, change)
        if (context.key === "color") {
            const style = context.displayedStyle
            return context.controller.setPhotoLayerTextStyle(context.layerIndex,
                String(style.family || "Helvetica"), Number(style.size || 48), String(color),
                Number(style.weight || 400) >= 600 ? "bold" : "regular", context.alignment)
        }
        return context.controller.setPhotoLayerTypography(context.layerIndex, change)
    }

    function beginColorTransaction(context) {
        if (!context || context.draftSession || context.outerTextSession ||
                typeof context.controller.beginCoalescedEdit !== "function") return
        context.controller.beginCoalescedEdit()
        context.transactionOpen = true
    }

    function finishColorTransaction(context, commit) {
        if (!context || !context.transactionOpen) return
        context.transactionOpen = false
        if (commit) context.controller.endCoalescedEdit()
        else context.controller.cancelCoalescedEdit()
    }

    function applyQuickColor(rgb) {
        const context = root.prepareColorContext("color")
        if (!context) return false
        const incoming = String(rgb || "")
        if (!/^#(?:[0-9a-fA-F]{6})$/.test(incoming)) return false
        const selected = Qt.rgba(Number("0x" + incoming.slice(1, 3)) / 255,
            Number("0x" + incoming.slice(3, 5)) / 255,
            Number("0x" + incoming.slice(5, 7)) / 255, context.fillColor.a)
        const value = root.colorToArgb(selected)
        root.beginColorTransaction(context)
        const applied = root.applyColorToContext(value, context)
        root.finishColorTransaction(context, applied)
        return applied
    }

    function openColorDialog(dialog, key) {
        if (root.locked) return false
        if (root.colorEditContext) root.finishColorDialog(false)
        const context = root.prepareColorContext(key)
        if (!context) return false
        const initial = String(key) === "strokeColor"
            ? String(context.displayedStyle.strokeColor || "#ff000000")
            : String(key) === "shadowColor"
                ? String(context.style.shadowColor || "#a0000000")
                : String(context.displayedStyle.color || "#ffffffff")
        context.lastAppliedColor = initial
        root.colorEditContext = context
        root.activeColorDialog = dialog
        root.editingEffectColorKey = String(key)
        root.beginColorTransaction(context)
        dialog.selectedColor = initial
        dialog.open()
        return true
    }

    function previewDialogColor(color) {
        const context = root.colorEditContext
        if (!context || context.draftSession || context.outerTextSession) return
        if (!root.colorContextIsCurrent(context)) {
            root.finishColorDialog(false)
            return
        }
        root.pendingDialogColor = root.colorToArgb(color)
        if (!dialogColorTimer.running) dialogColorTimer.start()
    }

    function applyPendingDialogColor() {
        dialogColorTimer.stop()
        const context = root.colorEditContext
        if (!context || !root.pendingDialogColor || context.draftSession || context.outerTextSession)
            return true
        if (!root.colorContextIsCurrent(context)) {
            root.finishColorDialog(false)
            return false
        }
        const value = root.pendingDialogColor
        root.pendingDialogColor = ""
        if (value === context.lastAppliedColor) return true
        if (!root.applyColorToContext(value, context)) {
            root.finishColorDialog(false)
            return false
        }
        context.lastAppliedColor = value
        return true
    }

    function finishColorDialog(commit, color) {
        dialogColorTimer.stop()
        const context = root.colorEditContext
        if (!context) return
        let applied = true
        if (commit) {
            if (!root.colorContextIsCurrent(context)) applied = false
            else {
                const value = root.colorToArgb(color)
                if (context.draftSession || context.outerTextSession || value !== context.lastAppliedColor)
                    applied = root.applyColorToContext(value, context)
            }
        }
        root.finishColorTransaction(context, !!commit && applied)
        root.colorEditContext = null
        root.activeColorDialog = null
        root.pendingDialogColor = ""
        if (!commit || !applied) return false
        return true
    }

    function cancelColorDialogForContextChange() {
        if (!root.colorEditContext) return
        const dialog = root.activeColorDialog
        root.finishColorDialog(false)
        if (dialog && dialog.visible) dialog.close()
    }

    function validateColorEditContexts() {
        if (root.colorEditContext && !root.colorContextIsCurrent(root.colorEditContext))
            root.cancelColorDialogForContextChange()
        if (root.colorOpacityContext && !root.colorContextIsCurrent(root.colorOpacityContext))
            root.finishColorOpacityEdit(false)
    }

    function beginColorOpacityEdit() {
        if (root.locked || root.colorOpacityContext) return false
        const context = root.prepareColorContext("color")
        if (!context) return false
        context.lastAppliedColor = root.colorToArgb(context.fillColor)
        root.colorOpacityContext = context
        root.pendingColorOpacity = context.fillColor.a
        root.beginColorTransaction(context)
        return true
    }

    function updateColorOpacity(value) {
        const context = root.colorOpacityContext
        if (!context || !root.colorContextIsCurrent(context)) {
            root.finishColorOpacityEdit(false)
            return false
        }
        root.pendingColorOpacity = Math.max(0, Math.min(1, Number(value)))
        if (!context.draftSession && !context.outerTextSession && !colorOpacityTimer.running)
            colorOpacityTimer.start()
        return true
    }

    function applyPendingColorOpacity() {
        colorOpacityTimer.stop()
        const context = root.colorOpacityContext
        if (!context || root.pendingColorOpacity < 0 ||
                !root.colorContextIsCurrent(context)) return false
        const color = context.fillColor
        const value = root.colorToArgb(Qt.rgba(color.r, color.g, color.b, root.pendingColorOpacity))
        if (value === context.lastAppliedColor) return true
        if (!root.applyColorToContext(value, context)) return false
        context.lastAppliedColor = value
        return true
    }

    function finishColorOpacityEdit(commit) {
        colorOpacityTimer.stop()
        const context = root.colorOpacityContext
        if (!context) return
        let applied = true
        if (commit && pendingColorOpacity >= 0) {
            const color = context.fillColor
            const value = root.colorToArgb(Qt.rgba(color.r, color.g, color.b, pendingColorOpacity))
            if (context.draftSession || context.outerTextSession || value !== context.lastAppliedColor)
                applied = root.applyColorToContext(value, context)
        }
        root.finishColorTransaction(context, !!commit && applied)
        root.colorOpacityContext = null
        root.pendingColorOpacity = -1
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
        const current = String(root.displayedStyle.family || "")
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
        if (root.textSelectionActive) {
            root.applyCharacterStyle({family: family, weight: preset.weight, italic: preset.italic,
                underline: false, color: preset.color, strokeWidth: preset.strokeWidth,
                strokeColor: preset.strokeColor || "#ff000000"})
            root.presetFontNotice = qsTr("Selection presets change font, weight, fill, and outline. Use Whole layer for spacing, shadows, and materials.")
            return
        }
        if (!root.multiTarget)
            changes.axes = ({})
        root.applyTypographyChanges(changes)
    }

    Components.Theme { id: theme }
    spacing: theme.space4
    onTargetIdChanged: {
        cancelColorDialogForContextChange()
        finishColorOpacityEdit(false)
        forceWholeLayerScope = false
        expanded = true
        axesExpanded = false
        presetFontNotice = ""
    }
    onLayerIndexChanged: validateColorEditContexts()
    onSelectedLayerIndicesChanged: validateColorEditContexts()
    onSelectedLayerIdsChanged: validateColorEditContexts()
    onDraftSessionChanged: validateColorEditContexts()
    onContentEditorChanged: validateColorEditContexts()
    onLockedChanged: if (locked) { cancelColorDialogForContextChange(); finishColorOpacityEdit(false) }
    onForceWholeLayerScopeChanged: validateColorEditContexts()
    onTextSelectionStartChanged: { forceWholeLayerScope = false; validateColorEditContexts() }
    onTextSelectionEndChanged: { forceWholeLayerScope = false; validateColorEditContexts() }
    onTextSelectionContentChanged: validateColorEditContexts()
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
        showLeadingIcon: true
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
                : root.controller.photoTextFontWarning(String(root.displayedStyle.family || "Helvetica")).length > 0
            wrapMode: Text.WordWrap
            text: !root.upgraded
                ? root.controller.photoTextFontWarning(String(root.displayedStyle.family || "Helvetica"))
                : root.layoutInfo.missingFont
                    ? qsTr("%1 is unavailable. Displaying %2; the saved font choice is preserved.").arg(root.displayedStyle.family).arg(root.layoutInfo.resolvedFamily)
                    : qsTr("The saved font face is unavailable. Choose an installed face to replace it.")
            color: theme.warning
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
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
            objectName: "photoTypographySelectionScope"
            Layout.fillWidth: true
            visible: root.textSelectionAvailable
            spacing: theme.space4
            Label {
                objectName: "photoTypographySelectionNotice"
                Layout.fillWidth: true
                text: root.textSelectionActive
                    ? qsTr("Selected text · %1–%2")
                        .arg(root.textSelectionStart + 1).arg(root.textSelectionEnd)
                    : qsTr("Whole layer")
                color: root.textSelectionActive ? theme.primaryAccent : theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
                Accessible.name: text
            }
            Components.MiniButton {
                objectName: "photoTypographyWholeLayerScopeButton"
                text: root.textSelectionActive ? qsTr("Whole layer") : qsTr("Selection")
                role: root.textSelectionActive ? "secondary" : "ghost"
                tooltip: root.textSelectionActive
                    ? qsTr("Apply typography to the complete text layer.")
                    : qsTr("Return to formatting only the selected characters.")
                onClicked: root.forceWholeLayerScope = root.textSelectionActive
            }
        }

        Label {
            objectName: "photoTypographyTypefaceHeading"
            Layout.fillWidth: true
            text: qsTr("Typeface")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: theme.weightControl
        }
        Components.PhotoFontPicker {
            id: fontFamilyPicker
            objectName: "photoTypographyFamilyPicker"
            triggerObjectName: "photoLayerTextFontField"
            controller: root.controller
            currentFamily: String(root.displayedStyle.family || "Helvetica")
            previewText: root.textSelectionActive
                ? root.textSelectionContent.slice(root.textSelectionStart, root.textSelectionEnd)
                : root.draftSession && root.draftSession.draftText !== undefined
                    ? String(root.draftSession.draftText)
                    : String(root.layerData.text === undefined ? "" : root.layerData.text)
            enabled: !root.locked
            Layout.fillWidth: true
            Layout.preferredHeight: theme.controlHeightDense
            onFamilySelected: function(family) { root.chooseFont(family) }
        }

        Label {
            objectName: "photoTypographyFaceSizeHeading"
            Layout.fillWidth: true
            text: root.multiTarget || root.textSelectionActive ? qsTr("Size") : qsTr("Face and size")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: theme.weightControl
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
                visible: !root.multiTarget && !root.textSelectionActive
                enabled: !root.locked
                readonly property var faces: root.controller.availablePhotoTextFaces(String(root.displayedStyle.family || ""))
                model: [qsTr("Default face")].concat(faces)
                currentIndex: Math.max(0, faces.indexOf(String(root.displayedStyle.face || "")) + 1)
                Accessible.name: qsTr("Font face")
                onActivated: function(index) {
                    root.applyTypographyChanges({
                        face: index === 0 ? "" : faces[index - 1],
                        axes: ({})
                    })
                }
            }
            Components.MiniButton {
                objectName: "photoTypographySizeDecrease"
                Layout.preferredWidth: theme.iconButtonSizeCompact
                Layout.preferredHeight: theme.controlHeightDense
                iconName: "minus"
                role: "icon"
                enabled: !root.locked && Number(root.displayedStyle.size || 48) > 0.25
                Accessible.name: qsTr("Decrease text size by one pixel")
                tooltip: Accessible.name
                onClicked: root.adjustTextSize(-1)
            }
            Components.ToneTextField {
                numericEntry: true
                id: sizeField
                objectName: "photoTypography_size"
                Layout.preferredWidth: theme.scaled(72)
                Layout.preferredHeight: theme.controlHeightDense
                enabled: !root.locked
                text: Number(root.displayedStyle.size || 48).toFixed(2).replace(/\.?0+$/, "")
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
                    text = Qt.binding(() => Number(root.displayedStyle.size || 48).toFixed(2).replace(/\.?0+$/, ""))
                }
                function commitSizeEntry() {
                    if (acceptableInput)
                        root.applyCoreStyle({size: Number(text)})
                    restore()
                }
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onEditingFinished: commitSizeEntry()
                Keys.onEscapePressed: function(event) {
                    restore()
                    focus = false
                    event.accepted = true
                }
            }
            Components.MiniButton {
                objectName: "photoTypographySizeIncrease"
                Layout.preferredWidth: theme.iconButtonSizeCompact
                Layout.preferredHeight: theme.controlHeightDense
                iconName: "plus"
                role: "icon"
                enabled: !root.locked && Number(root.displayedStyle.size || 48) < 8192
                Accessible.name: qsTr("Increase text size by one pixel")
                tooltip: Accessible.name
                onClicked: root.adjustTextSize(1)
            }
            Label {
                text: qsTr("px")
                color: theme.textMuted
                font.family: theme.fontNumeric
                font.features: theme.numericFontFeatures
                font.pixelSize: theme.typeCaption
            }
        }

        Label {
            objectName: "photoTypographyCharacterHeading"
            Layout.fillWidth: true
            text: qsTr("Character")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: theme.weightControl
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
                active: Number(root.displayedStyle.weight || 400) >= 600
                enabled: !root.locked
                Accessible.name: qsTr("Bold")
                tooltip: active ? qsTr("Use regular weight.") : qsTr("Use bold weight.")
                onClicked: root.applyCoreStyle(root.upgraded && !root.textSelectionActive
                    ? {weight: active ? 400 : 700, face: ""}
                    : {weight: active ? 400 : 700})
            }
            Components.MiniButton {
                objectName: "photoTypographyItalicButton"
                Layout.preferredWidth: theme.scaled(32)
                visible: root.upgraded
                text: "I"
                role: "segmented"
                active: !!root.displayedStyle.italic
                enabled: !root.locked
                Accessible.name: qsTr("Italic")
                tooltip: active ? qsTr("Turn italic off.") : qsTr("Turn italic on.")
                onClicked: root.applyCoreStyle(root.upgraded && !root.textSelectionActive
                    ? {italic: !root.displayedStyle.italic, face: ""}
                    : {italic: !root.displayedStyle.italic})
            }
            Components.MiniButton {
                objectName: "photoTypographyUnderlineButton"
                Layout.preferredWidth: theme.scaled(32)
                visible: root.upgraded
                text: "U"
                role: "segmented"
                active: !!root.displayedStyle.underline
                enabled: !root.locked
                Accessible.name: qsTr("Underline")
                tooltip: active ? qsTr("Remove underline.") : qsTr("Underline text.")
                onClicked: root.applyCoreStyle({underline: !root.displayedStyle.underline})
            }
            Components.ColorSwatchButton {
                objectName: "photoTypographyFillColorSwatch"
                swatchColor: String(root.displayedStyle.color || "#ffffffff")
                label: qsTr("Text fill: %1").arg(String(root.displayedStyle.color || "#ffffffff"))
                enabled: !root.locked
                onPicked: root.openColorDialog(fillColorDialog, "color")
            }
            Components.ToneTextField {
                id: fillColorField
                objectName: "photoLayerTextColorField"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: theme.controlHeightDense
                enabled: !root.locked
                text: String(root.displayedStyle.color || "#ffffffff")
                placeholderText: qsTr("#RRGGBB or #AARRGGBB")
                Accessible.name: qsTr("Text fill color")
                selectByMouse: true
                validator: RegularExpressionValidator {
                    regularExpression: /^#?(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
                }
                onEditingFinished: {
                    if (acceptableInput)
                        root.applyColor(text)
                    text = Qt.binding(() => String(root.displayedStyle.color || "#ffffffff"))
                }
            }
        }

        ColumnLayout {
            objectName: "photoTypographyQuickColorPalette"
            Layout.fillWidth: true
            Label {
                text: qsTr("Quick colors")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
            RowLayout {
                objectName: "photoTypographyQuickColorSwatches"
                Layout.fillWidth: true
                spacing: theme.space2
                Repeater {
                    model: ["#FFF7EA", "#1F2126", "#E65A52", "#E5A342", "#50A89A", "#5687D8"]
                    delegate: Components.ColorSwatchButton {
                        required property int index
                        required property string modelData
                        objectName: "photoTypographyQuickColor" + index
                        Layout.preferredWidth: theme.iconButtonSizeCompact
                        Layout.preferredHeight: theme.controlHeightDense
                        swatchColor: modelData
                        label: qsTr("Set text color to %1; keep current opacity").arg(modelData)
                        enabled: !root.locked
                        onPicked: root.applyQuickColor(modelData)
                    }
                }
            }
        }

        RowLayout {
            objectName: "photoTypographyFillOpacityRow"
            Layout.fillWidth: true
            spacing: theme.space4
            Label {
                text: qsTr("Opacity")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                Accessible.name: qsTr("Text fill opacity")
            }
            Components.ToolValueSlider {
                id: fillOpacitySlider
                objectName: "photoTypographyFillOpacity"
                Layout.fillWidth: true
                Layout.minimumWidth: 48
                Layout.preferredHeight: theme.controlHeightDense
                from: 0
                to: 1
                stepSize: 0.01
                value: root.displayedFillColor.a
                enabled: !root.locked
                label: qsTr("Text opacity")
                valueText: Math.round(value * 100) + "%"
                showCaption: false
                Accessible.name: qsTr("Text fill opacity")
                Accessible.description: qsTr("Adjust fill opacity from transparent to opaque.")
                onPressedChanged: {
                    if (pressed) root.beginColorOpacityEdit()
                    else root.finishColorOpacityEdit(true)
                }
                onMoved: root.updateColorOpacity(value)
            }
            Label {
                objectName: "photoTypographyFillOpacityValue"
                Layout.preferredWidth: theme.scaled(34)
                text: Math.round(fillOpacitySlider.value * 100) + "%"
                color: theme.textMuted
                font.family: theme.fontNumeric
                font.features: theme.numericFontFeatures
                font.pixelSize: theme.typeCaption
                horizontalAlignment: Text.AlignRight
                Accessible.name: qsTr("Text fill opacity percentage")
            }
        }

        ColumnLayout {
            objectName: "photoTypographySelectionOutlineControls"
            Layout.fillWidth: true
            visible: root.textSelectionActive
            spacing: theme.space2
            Label {
                Layout.fillWidth: true
                text: qsTr("Selected text outline")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space4
                Components.ToneTextField {
                    id: selectionStrokeWidthField
                    objectName: "photoTypographySelectionStrokeWidth"
                    numericEntry: true
                    Layout.preferredWidth: theme.scaled(78)
                    Layout.preferredHeight: theme.controlHeightDense
                    enabled: !root.locked
                    text: Number(root.displayedStyle.strokeWidth || 0).toFixed(2).replace(/\.?0+$/, "")
                    placeholderText: qsTr("0 px")
                    Accessible.name: qsTr("Selected text outline width in pixels")
                    selectByMouse: true
                    validator: DoubleValidator {
                        bottom: 0
                        top: 256
                        decimals: 2
                        notation: DoubleValidator.StandardNotation
                        locale: "C"
                    }
                    onEditingFinished: {
                        if (acceptableInput) root.applyCoreStyle({strokeWidth: Number(text)})
                        text = Qt.binding(() => Number(root.displayedStyle.strokeWidth || 0)
                            .toFixed(2).replace(/\.?0+$/, ""))
                    }
                }
                Components.ColorSwatchButton {
                    objectName: "photoTypographySelectionStrokeColorSwatch"
                    Layout.preferredWidth: theme.scaled(34)
                    Layout.preferredHeight: theme.controlHeightDense
                    swatchColor: String(root.displayedStyle.strokeColor || "#ff000000")
                    label: qsTr("Selected text outline color")
                    enabled: !root.locked
                    onPicked: root.openColorDialog(selectionStrokeColorDialog, "strokeColor")
                }
                Components.ToneTextField {
                    id: selectionStrokeColorField
                    objectName: "photoTypographySelectionStrokeColor"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: theme.controlHeightDense
                    enabled: !root.locked
                    text: String(root.displayedStyle.strokeColor || "#ff000000")
                    placeholderText: qsTr("#AARRGGBB")
                    Accessible.name: qsTr("Selected text outline color")
                    validator: RegularExpressionValidator {
                        regularExpression: /^#?(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
                    }
                    onEditingFinished: {
                        if (acceptableInput) root.applyCoreStyle({strokeColor: text})
                        text = Qt.binding(() => String(root.displayedStyle.strokeColor || "#ff000000"))
                    }
                }
            }
        }

        Label {
            objectName: "photoTypographyParagraphHeading"
            Layout.fillWidth: true
            text: qsTr("Paragraph alignment")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: theme.weightControl
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
            showLeadingIcon: true
            role: "secondary"
            onClicked: root.applyCoreStyle({size: Number(root.style.size || 48)})
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.upgraded
            Label {
                Layout.fillWidth: true
                text: qsTr("Style presets")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: theme.weightControl
            }
            Components.MiniButton {
                objectName: "photoTypographyStylePresetToggle"
                Layout.preferredWidth: theme.scaled(78)
                text: root.stylePresetsExpanded ? qsTr("Hide") : qsTr("Show")
                iconName: root.stylePresetsExpanded ? "chevron-down" : "chevron-right"
                showLeadingIcon: true
                role: "ghost"
                active: root.stylePresetsExpanded
                Accessible.name: root.stylePresetsExpanded ? qsTr("Hide style presets") : qsTr("Show style presets")
                tooltip: qsTr("Choose a starting look, then customize the type.")
                onClicked: root.stylePresetsExpanded = !root.stylePresetsExpanded
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.upgraded && root.stylePresetsExpanded
            text: qsTr("Start with a look, then adjust its type, spacing, and finish below.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
            wrapMode: Text.WordWrap
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
                enabled: !root.locked && root.visibleStylePresetIndices.length > 0
                model: root.visibleStylePresetIndices.map(index => root.stylePresets[index].name)
                currentIndex: root.visibleStylePresetIndices.indexOf(root.selectedPresetIndex)
                Accessible.name: qsTr("Typography style preset")
                Accessible.description: root.textSelectionActive
                    ? qsTr("Choose a character style for the selected text.")
                    : qsTr("Choose a complete starting style for this text layer.")
                onActivated: function(index) {
                    if (index >= 0 && index < root.visibleStylePresetIndices.length)
                        root.selectedPresetIndex = root.visibleStylePresetIndices[index]
                }
            }
            Components.MiniButton {
                objectName: "photoTypographyStylePresetFavorite"
                Layout.preferredWidth: theme.iconButtonSizeCompact
                Layout.preferredHeight: theme.controlHeightDense
                iconName: "star"
                showLeadingIcon: true
                role: "icon"
                active: root.selectedPresetIsFavorite
                enabled: !root.locked &&
                    root.visibleStylePresetIndices.indexOf(root.selectedPresetIndex) >= 0
                Accessible.name: root.selectedPresetIsFavorite
                    ? qsTr("Remove selected style from favorites")
                    : qsTr("Add selected style to favorites")
                tooltip: Accessible.name
                onClicked: root.toggleSelectedPresetFavorite()
            }
            Components.MiniButton {
                objectName: "photoTypographyStylePresetApply"
                Layout.preferredWidth: theme.scaled(72)
                text: qsTr("Apply")
                role: "primary"
                enabled: !root.locked && root.visibleStylePresetIndices.indexOf(root.selectedPresetIndex) >= 0
                Accessible.name: qsTr("Apply typography style preset")
                tooltip: root.textSelectionActive
                    ? qsTr("Apply font, weight, fill, and outline to the selected characters.")
                    : qsTr("Apply the selected font, color, spacing, and effects to this text.")
                onClicked: root.applyStylePreset()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.upgraded && root.stylePresetsExpanded
            spacing: theme.space4
            Components.MiniButton {
                objectName: "photoTypographyFavoriteFilter"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.favoritePresetsOnly ? qsTr("Show all styles") : qsTr("Show favorites")
                iconName: root.favoritePresetsOnly ? "list" : "star"
                showLeadingIcon: true
                allowLabelWrap: true
                role: root.favoritePresetsOnly ? "secondary" : "ghost"
                active: root.favoritePresetsOnly
                enabled: !root.locked
                Accessible.name: root.favoritePresetsOnly
                    ? qsTr("Show all typography styles")
                    : qsTr("Show favorite typography styles")
                tooltip: root.favoritePresetsOnly
                    ? qsTr("Return to the complete set of built-in styles.")
                    : qsTr("Filter the list to styles you have starred.")
                onClicked: root.toggleFavoritePresetFilter()
            }
            Label {
                objectName: "photoTypographyFavoriteCount"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: root.favoritePresetsOnly && root.favoritePresetNames.length > 0
                text: root.favoritePresetNames.length > 0
                    ? qsTr("%1 saved style%2").arg(root.favoritePresetNames.length)
                        .arg(root.favoritePresetNames.length === 1 ? "" : "s")
                    : qsTr("Star a style to save it here.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
            }
        }
        Label {
            objectName: "photoTypographyNoFavoritesNotice"
            Layout.fillWidth: true
            visible: root.upgraded && root.stylePresetsExpanded && root.favoritePresetsOnly &&
                root.favoritePresetNames.length === 0
            text: qsTr("Choose Show all styles, pick a style, then tap its star.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
            wrapMode: Text.WordWrap
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

        GridLayout {
            objectName: "photoTypographyAdvancedNavigation"
            Layout.fillWidth: true
            visible: root.upgraded
            columns: root.width >= theme.scaled(520) ? 3 : 2
            columnSpacing: theme.space2
            rowSpacing: theme.space2
            Components.MiniButton {
                objectName: "photoTypographySpacingToggle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: qsTr("Spacing & layout")
                iconName: "geometry"
                showLeadingIcon: true
                allowLabelWrap: true
                role: "segmented"
                active: root.spacingExpanded
                Accessible.name: qsTr("Spacing and layout controls")
                tooltip: qsTr("Choose point or paragraph text, set spacing, shift the baseline, or tune variable font axes.")
                onClicked: root.toggleAdvancedSection("layout")
            }
            Components.MiniButton {
                objectName: "photoTypographyEffectsToggle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: qsTr("Outline & shadow")
                iconName: "effects"
                showLeadingIcon: true
                allowLabelWrap: true
                role: "segmented"
                active: root.effectsExpanded
                Accessible.name: qsTr("Outline, shadow, and material controls")
                tooltip: qsTr("Add an outline or shadow, or apply glass and metal materials.")
                onClicked: root.toggleAdvancedSection("finish")
            }
            Components.MiniButton {
                objectName: "photoTypographyPathToggle"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                visible: !root.multiTarget
                text: qsTr("Text on a path")
                iconName: "curve"
                showLeadingIcon: true
                allowLabelWrap: true
                role: "segmented"
                active: root.pathExpanded
                Accessible.name: qsTr("Text path controls")
                tooltip: qsTr("Attach text to a selection contour and edit its baseline or flow direction.")
                onClicked: root.toggleAdvancedSection("path")
            }
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
            Label {
                Layout.fillWidth: true
                text: qsTr("Spacing and baseline")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: theme.weightControl
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Letter spacing changes the gap between characters. Line height controls the distance between lines.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                wrapMode: Text.WordWrap
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= theme.scaled(460) ? 2 : 1
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
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }
                        Components.ToneTextField {
                            numericEntry: true
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
                showLeadingIcon: true
                role: "ghost"
                active: root.axesExpanded
                tooltip: qsTr("Adjust font-specific controls such as width, weight, or optical size when the selected face supports them.")
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
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }
                        Components.ToneTextField {
                            numericEntry: true
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

        ColumnLayout {
            objectName: "photoTypographyEffectsControls"
            Layout.fillWidth: true
            visible: root.upgraded && root.effectsExpanded
            spacing: theme.space4
            Label {
                Layout.fillWidth: true
                text: qsTr("Outline, shadow, and materials")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: theme.weightControl
            }
            Label {
                Layout.fillWidth: true
                text: root.textSelectionActive
                    ? qsTr("Outline follows the selected text. Shadow and material settings affect the whole layer.")
                    : qsTr("Use an outline for contrast, a small shadow for separation, or a material to blend text with the photo.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                wrapMode: Text.WordWrap
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= theme.scaled(460) ? 2 : 1
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
                            Layout.fillWidth: true
                            text: effectMetric.modelData.label
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WordWrap
                        }
                        Components.ToneTextField {
                            numericEntry: true
                            objectName: "photoTypography_" + effectMetric.modelData.key
                            Layout.fillWidth: true
                            Layout.preferredHeight: theme.controlHeightDense
                            enabled: !root.locked
                text: String(root.displayedStyle[effectMetric.modelData.key] || 0)
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
                                text = Qt.binding(() => String(root.displayedStyle[effectMetric.modelData.key] || 0))
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space4
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
                            Layout.fillWidth: true
                            text: effectColor.modelData.label
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space3
                            Components.ColorSwatchButton {
                                objectName: "photoTypography_" + effectColor.modelData.key + "Swatch"
                                Layout.preferredWidth: theme.iconButtonSizeCompact
                                Layout.preferredHeight: theme.controlHeightDense
                                swatchColor: root.effectColorValue(effectColor.modelData.key)
                                label: effectColor.modelData.label
                                enabled: !root.locked
                                onPicked: root.openEffectColorDialog(effectColor.modelData.key)
                            }
                            Components.ToneTextField {
                                objectName: "photoTypography_" + effectColor.modelData.key + "Field"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredHeight: theme.controlHeightDense
                                enabled: !root.locked
                                Accessible.name: effectColor.modelData.label
                                text: root.effectColorValue(effectColor.modelData.key)
                                placeholderText: qsTr("#AARRGGBB")
                                validator: RegularExpressionValidator {
                                    regularExpression: /^#?(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
                                }
                                onEditingFinished: {
                                    if (acceptableInput) {
                                        const change = ({})
                                        change[effectColor.modelData.key] = text
                                        root.applyCoreStyle(change)
                                    }
                                    text = Qt.binding(() => root.effectColorValue(effectColor.modelData.key))
                                }
                            }
                        }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Photo material")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: theme.weightControl
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Glass and metal styles use the image behind the letters to create their surface.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                wrapMode: Text.WordWrap
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
                    showLeadingIcon: true
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

        ColumnLayout {
            objectName: "photoTypographyPathControls"
            Layout.fillWidth: true
            visible: root.upgraded && root.pathExpanded && !root.multiTarget
            spacing: theme.space4
            Label {
                Layout.fillWidth: true
                text: qsTr("Follow a selection outline with text, then adjust the baseline and direction on the photo.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space4
                Components.MiniButton {
                    objectName: "photoTextPathFromSelection"
                    Layout.fillWidth: true
                    text: root.style.pathPoints && root.style.pathPoints.length
                        ? qsTr("Replace contour") : qsTr("Use selection")
                    iconName: "masks"
                    showLeadingIcon: true
                    allowLabelWrap: true
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
                    showLeadingIcon: true
                    allowLabelWrap: true
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
                    objectName: "photoTextPathSide"
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
                    objectName: "photoTextPathStart"
                    Layout.fillWidth: true
                    enabled: !root.locked
                    showCaption: true
                    from: 0; to: 1; stepSize: 0.01
                    value: Number(root.style.pathStart || 0)
                    label: qsTr("Start")
                    valueText: Math.round(value * 100) + "%"
                    onMoved: root.controller.setPhotoLayerTypography(root.layerIndex, {pathStart: value})
                }
                Components.ToolValueSlider {
                    objectName: "photoTextPathEnd"
                    Layout.fillWidth: true
                    enabled: !root.locked
                    showCaption: true
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
                    objectName: "photoTextPathOffset"
                    Layout.fillWidth: true
                    enabled: !root.locked
                    showCaption: true
                    from: -128; to: 128; stepSize: 1
                    value: Number(root.style.pathOffset || 0)
                    label: qsTr("Offset")
                    valueText: Math.round(value) + " px"
                    onMoved: root.controller.setPhotoLayerTypography(root.layerIndex, {pathOffset: value})
                }
                Components.ToolValueSlider {
                    objectName: "photoTextPathSpacing"
                    Layout.fillWidth: true
                    enabled: !root.locked
                    showCaption: true
                    from: 0; to: 32; stepSize: 0.25
                    value: Number(root.style.pathSpacing || 0)
                    label: qsTr("Spacing")
                    valueText: value.toFixed(1) + " px"
                    onMoved: root.controller.setPhotoLayerTypography(root.layerIndex, {pathSpacing: value})
                }
            }
        }
    }

    Timer {
        id: dialogColorTimer
        interval: 16
        repeat: false
        onTriggered: {
            if (!root.applyPendingDialogColor()) root.finishColorDialog(false)
        }
    }
    Timer {
        id: colorOpacityTimer
        interval: 16
        repeat: false
        onTriggered: {
            if (!root.applyPendingColorOpacity()) root.finishColorOpacityEdit(false)
        }
    }

    ColorDialog {
        id: fillColorDialog
        title: qsTr("Text fill color")
        options: ColorDialog.ShowAlphaChannel
        selectedColor: root.displayedStyle.color || "#ffffffff"
        onSelectedColorChanged: root.previewDialogColor(selectedColor)
        onAccepted: root.finishColorDialog(true, selectedColor)
        onRejected: root.finishColorDialog(false)
    }
    ColorDialog {
        id: selectionStrokeColorDialog
        title: qsTr("Selected text outline color")
        options: ColorDialog.ShowAlphaChannel
        selectedColor: root.displayedStyle.strokeColor || "#ff000000"
        onSelectedColorChanged: root.previewDialogColor(selectedColor)
        onAccepted: root.finishColorDialog(true, selectedColor)
        onRejected: root.finishColorDialog(false)
    }
    ColorDialog {
        id: effectColorDialog
        objectName: "photoTypographyEffectColorDialog"
        title: root.editingEffectColorKey === "strokeColor"
            ? qsTr("Outline color") : qsTr("Shadow color")
        options: ColorDialog.ShowAlphaChannel
        onSelectedColorChanged: root.previewDialogColor(selectedColor)
        onAccepted: root.finishColorDialog(true, selectedColor)
        onRejected: root.finishColorDialog(false)
    }
}
