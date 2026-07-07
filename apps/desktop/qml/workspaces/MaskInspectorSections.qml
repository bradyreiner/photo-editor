import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"

Column {
    id: root

    required property var editRoot
    Theme { id: theme }
    property string maskSearchQuery: ""
    property bool diagnosticMaskPreviewModesVisible: false
    property bool runtimeDiagnosticsExpanded: false
    readonly property string normalMaskDisplayContract: "normal-mask-display|overlay|outline|marching-ants|hidden|single-color-overlay|diagnostics-collapsed:v2"
    readonly property string rubylithOverlayPresetContract: "rubylith-overlay-preset:v1|display-mode:overlay|color:#ff2f5f|single-color:true|separate-from-diagnostics:true"
    readonly property string diagnosticMaskDisplayContract: "diagnostic-mask-display|grayscale-alpha|isolated-mask|advanced-toggle|required-for-matte-inspection:v2"
    readonly property string developerDiagnosticsMenuContract: "developer-diagnostics-menu:v2|developer-mode-gated:true|normal-mask-modes:separate|matte-debug:collapsed|runtime-detail:collapsed|raw-alpha:diagnostic-only|confidence:false-color-focus:diagnostic-only|default-visible:false"
    readonly property string runtimeDiagnosticsVisibilityContract: "runtime-diagnostics:v1|chips:always|raw-status:details-only|default-expanded:false|toggle:available|no-debug-text-at-rest:true|functionality:preserved"
    readonly property string maskLayerParameterTerminologyContract: "mask-layer-parameter-terminology:v1|layer-opacity:composite-layer-result|mask-density:alpha-strength-before-gating|mask-opacity:mask-source-contribution|brush-flow:per-stamp-paint-rate|brush-opacity:pressure-or-source-opacity|overlay-opacity:visualization-only"
    readonly property string adjustmentControlTargetSupportContract: "adjustment-control-target-support:v1|targets:base-image,adjustment-layer,photo-layer,mask|default:base-image+adjustment-layer|mask-controls:mask-only|tool-controls:mask-source-only"

    component MaskSection: InspectorSection {
        showAccentMarker: false
        uppercaseTitle: false
        titleFontFamily: theme.fontDisplay
        titlePixelSize: 16
        titleWeight: Font.Bold
        titleLetterSpacing: 0
    }

    readonly property var activeMaskData: editorPreview.hasActiveMask &&
                                           editorPreview.activeMaskIndex >= 0 &&
                                           editorPreview.activeMaskIndex < editorPreview.maskStack.length
                                           ? editorPreview.maskStack[editorPreview.activeMaskIndex]
                                           : null
    readonly property color activeMaskAccent: activeMaskData ? editRoot.maskAccent(activeMaskData) : theme.accentCool400
    readonly property color maskPreviewOverlayResolvedColor:
        String(editorPreview.maskPreviewOverlayColor || "").length > 0
        ? editorPreview.maskPreviewOverlayColor
        : root.activeMaskAccent

    ColorDialog {
        id: maskPreviewOverlayColorDialog
        objectName: "maskPreviewOverlayColorDialog"
        title: "Selection Overlay Color"
        selectedColor: root.maskPreviewOverlayResolvedColor
        onAccepted: editorPreview.maskPreviewOverlayColor = selectedColor
    }

    function smartSelectionRuntimeLabel() {
        return String(editorPreview.smartSelectionRuntimeLabel || "Local Smart Select")
    }

    function inpaintRuntimeLabel() {
        return String(editorPreview.inpaintRuntimeLabel || "Local CPU Fill")
    }

    function runtimeDiagnosticsSignature() {
        return "runtime-diagnostics:v1" +
                "|expanded:" + String(root.runtimeDiagnosticsExpanded) +
                "|selection:" + root.smartSelectionRuntimeLabel() +
                "|retouch:" + root.inpaintRuntimeLabel() +
                "|status:" + String(editorPreview.aiModelStatus || "").length +
                "|detail:" + String(editorPreview.aiModelDiagnostics || "").length +
                "|chips:always|raw:details-only"
    }

    function maskCompareGroupActive(groupKey) {
        return editorPreview.maskCompareAdjustmentGroups.indexOf(groupKey) >= 0
    }

    function toggleMaskCompareGroup(groupKey) {
        const groups = editorPreview.maskCompareAdjustmentGroups.slice(0)
        const existingIndex = groups.indexOf(groupKey)
        if (existingIndex >= 0)
            groups.splice(existingIndex, 1)
        else
            groups.push(groupKey)
        editorPreview.maskCompareAdjustmentGroups = groups
    }

    function maskControlTargetSupportSignature(rawTargets) {
        const source = String(rawTargets || "mask").split(",")
        const targets = []
        for (let index = 0; index < source.length; ++index) {
            const target = String(source[index] || "").trim().toLowerCase()
            if (["base-image", "adjustment-layer", "photo-layer", "mask"].indexOf(target) >= 0 &&
                    targets.indexOf(target) < 0)
                targets.push(target)
        }
        return root.adjustmentControlTargetSupportContract +
                "|base-image:" + String(targets.indexOf("base-image") >= 0) +
                "|adjustment-layer:" + String(targets.indexOf("adjustment-layer") >= 0) +
                "|photo-layer:" + String(targets.indexOf("photo-layer") >= 0) +
                "|mask:" + String(targets.indexOf("mask") >= 0)
    }

    function renameMaskRow(flatIndex) {
        editorPreview.activeMaskIndex = flatIndex
        activeMaskNameField.forceActiveFocus()
        activeMaskNameField.selectAll()
    }

    function toggleMaskRowEnabled(flatIndex, enabled) {
        editorPreview.activeMaskIndex = flatIndex
        editorPreview.activeMaskEnabled = !enabled
    }

    function toggleMaskRowSolo(flatIndex, solo) {
        editorPreview.activeMaskIndex = flatIndex
        editorPreview.activeMaskSolo = !solo
    }

    function toggleMaskRowLocked(flatIndex, locked) {
        editorPreview.activeMaskIndex = flatIndex
        editorPreview.activeMaskLocked = !locked
    }

    function moveMaskRow(flatIndex, offset) {
        editorPreview.activeMaskIndex = flatIndex
        editorPreview.moveActiveMask(offset)
    }

    function duplicateMaskRow(flatIndex) {
        editorPreview.activeMaskIndex = flatIndex
        editorPreview.duplicateActiveMask()
    }

    function deleteMaskRow(flatIndex) {
        editorPreview.activeMaskIndex = flatIndex
        editorPreview.removeActiveMask()
    }

    function applyRetouchRow(flatIndex) {
        editorPreview.activeMaskIndex = flatIndex
        editorPreview.applyActiveRetouch()
    }

    function cancelRetouchRow(flatIndex) {
        editorPreview.activeMaskIndex = flatIndex
        editorPreview.cancelActiveRetouch()
    }

    function retouchAccent(mode) {
        if (mode === "clone")
            return "#7cb5ff"
        if (mode === "heal")
            return "#72cbb2"
        if (mode === "dust")
            return "#c2b36a"
        if (mode === "red-eye")
            return "#d36a5e"
        if (mode === "skin-cleanup")
            return "#d7a08c"
        if (mode === "frequency-smoothing")
            return "#c994d8"
        return "#d98962"
    }

    function maskTreeSources(sources) {
        const input = Array.isArray(sources) ? sources : []
        const byParent = ({})
        const byId = ({})
        for (let i = 0; i < input.length; ++i) {
            const source = input[i]
            if (source.id && source.id.length > 0)
                byId[source.id] = source
            const parentId = source.parentId || ""
            if (!byParent[parentId])
                byParent[parentId] = []
            byParent[parentId].push(source)
        }

        const result = []
        const visited = ({})
        function appendSource(source, depth) {
            if (!source)
                return
            const key = source.id && source.id.length > 0 ? source.id : "flat-" + source.flatIndex
            if (visited[key])
                return
            visited[key] = true
            const node = ({})
            for (const propertyName in source)
                node[propertyName] = source[propertyName]
            node.depth = depth
            result.push(node)
            const children = byParent[source.id] || []
            for (let childIndex = 0; childIndex < children.length; ++childIndex)
                appendSource(children[childIndex], depth + 1)
        }

        const roots = []
        for (let sourceIndex = 0; sourceIndex < input.length; ++sourceIndex) {
            const source = input[sourceIndex]
            const parentId = source.parentId || ""
            if (parentId.length === 0 || !byId[parentId])
                roots.push(source)
        }
        for (let rootIndex = 0; rootIndex < roots.length; ++rootIndex)
            appendSource(roots[rootIndex], 0)
        for (let fallbackIndex = 0; fallbackIndex < input.length; ++fallbackIndex)
            appendSource(input[fallbackIndex], 0)
        return result
    }

    function maskMatchesSearch(source) {
        const query = root.maskSearchQuery.trim().toLowerCase()
        if (query.length === 0)
            return true
        const searchable = ((source.name || "") + " " +
                            (source.kind || "") + " " +
                            (source.operation || "") + " " +
                            (source.maskSetName || "")).toLowerCase()
        return searchable.indexOf(query) >= 0
    }

    function maskSearchText() {
        return root.maskSearchQuery.trim().toLowerCase()
    }

    function maskSearchMatches(text) {
        const query = root.maskSearchText()
        return query.length === 0 || String(text || "").toLowerCase().indexOf(query) >= 0
    }

    function maskSetMatchesSearch(maskSet) {
        if (root.maskSearchText().length === 0)
            return true
        if (root.maskSearchMatches((maskSet.name || "") + " " + (maskSet.id || "")))
            return true
        const sources = maskSet.sources || []
        for (let index = 0; index < sources.length; ++index) {
            if (root.maskMatchesSearch(sources[index]))
                return true
        }
        return false
    }

    function maskSearchToolItems() {
        return [
            { category: "Tool", label: "Smart Select", detail: "Create or refine with object selection", keywords: "smart select object subject ai local runtime", action: "smart" },
            { category: "Tool", label: "Brush Add", detail: "Paint an additive brush component", keywords: "brush add paint selection", action: "brush-add" },
            { category: "Tool", label: "Brush Subtract", detail: "Paint an erasing brush component", keywords: "brush subtract erase remove paint", action: "brush-subtract" },
            { category: "Tool", label: "Lasso", detail: "Draw a freeform selection", keywords: "lasso freeform manual selection", action: "lasso" },
            { category: "Tool", label: "Polygon", detail: "Click editable polygon points", keywords: "polygon manual points selection", action: "polygon" },
            { category: "Tool", label: "Linear", detail: "Create a linear gradient selection", keywords: "linear gradient mask", action: "linear" },
            { category: "Tool", label: "Radial", detail: "Create a radial selection", keywords: "radial ellipse circular selection", action: "radial" },
            { category: "Tool", label: "Luma Range", detail: "Select by luminance", keywords: "luma luminance range brightness", action: "luminance" },
            { category: "Tool", label: "Color Range", detail: "Select by sampled color", keywords: "color range hue saturation sample eyedropper", action: "color" },
            { category: "Tool", label: "Depth Range", detail: "Select by depth metadata", keywords: "depth range portrait", action: "depth" },
            { category: "Tool", label: "Subject", detail: "Create a subject selection", keywords: "subject foreground smart select generated", action: "subject" }
        ]
    }

    function maskSearchAdjustmentItems() {
        return [
            { category: "Control", label: "Exposure", detail: "Tone adjustment", keywords: "exposure tone ev light local adjustment" },
            { category: "Control", label: "Contrast", detail: "Tone adjustment", keywords: "contrast tone local adjustment" },
            { category: "Control", label: "Highlights", detail: "Tone adjustment", keywords: "highlights tone local adjustment" },
            { category: "Control", label: "Shadows", detail: "Tone adjustment", keywords: "shadows tone local adjustment" },
            { category: "Control", label: "Temperature", detail: "Color adjustment", keywords: "temperature warmth color white balance local adjustment" },
            { category: "Control", label: "Tint", detail: "Color adjustment", keywords: "tint color local adjustment" },
            { category: "Control", label: "Vibrance", detail: "Presence adjustment", keywords: "vibrance presence local adjustment" },
            { category: "Control", label: "Overall Saturation", detail: "Presence adjustment", keywords: "saturation chroma presence local adjustment" },
            { category: "Control", label: "Hue Shift", detail: "Presence adjustment", keywords: "hue shift color local adjustment" },
            { category: "Control", label: "Chroma Shift", detail: "Presence adjustment", keywords: "chroma saturation shift local adjustment" },
            { category: "Control", label: "Remove", detail: "Effects adjustment", keywords: "remove strength effects retouch local adjustment" },
            { category: "Control", label: "Heal Texture", detail: "Detail adjustment", keywords: "heal texture detail retouch local adjustment" },
            { category: "Control", label: "Heal Color", detail: "Detail adjustment", keywords: "heal color detail retouch local adjustment" }
        ]
    }

    function maskSearchResults() {
        const query = root.maskSearchText()
        if (query.length === 0)
            return []
        const results = []
        const addIfMatch = function(item) {
            if (root.maskSearchMatches(item.label + " " + item.detail + " " + item.keywords))
                results.push(item)
        }
        root.maskSearchToolItems().forEach(addIfMatch)
        const sets = editorPreview.maskSets || []
        for (let setIndex = 0; setIndex < sets.length; ++setIndex) {
            const sources = sets[setIndex].sources || []
            if (root.maskSearchMatches((sets[setIndex].name || "") + " mask components")) {
                results.push({ category: "Mask", label: sets[setIndex].name, detail: sources.length + " components", flatIndex: sources.length > 0 ? sources[0].flatIndex : -1 })
            }
            for (let sourceIndex = 0; sourceIndex < sources.length; ++sourceIndex) {
                const source = sources[sourceIndex]
                if (root.maskMatchesSearch(source)) {
                    results.push({ category: "Mask", label: source.name || source.kind, detail: (source.maskSetName || sets[setIndex].name || "Mask") + " · " + (source.operationBadge || source.operation || "add"), flatIndex: source.flatIndex })
                }
            }
        }
        root.maskSearchAdjustmentItems().forEach(addIfMatch)
        return results.slice(0, 12)
    }

    function activateMaskSearchResult(result) {
        if (result.flatIndex !== undefined && result.flatIndex >= 0) {
            editorPreview.activeMaskIndex = result.flatIndex
            return
        }
        if (result.action === "smart") {
            editorPreview.addMask("object")
        } else if (result.action === "brush-add") {
            editorPreview.startSelectionBrush(false)
        } else if (result.action === "brush-subtract") {
            editorPreview.startSelectionBrush(true)
        } else if (result.action === "lasso") {
            editorPreview.startLassoSelection(false)
        } else if (result.action === "polygon") {
            editorPreview.startPolygonSelection(false)
        } else if (result.action === "subject") {
            editorPreview.addAIMask("subject")
        } else if (result.action && result.action.length > 0) {
            editorPreview.addMask(result.action)
        }
    }

    function retouchModeLabel(modeName) {
        const mode = String(modeName || "").trim().toLowerCase()
        if (mode === "remove")
            return "Remove"
        if (mode === "heal")
            return "Heal"
        if (mode === "patch")
            return "Patch"
        if (mode === "clone")
            return "Clone"
        if (mode === "dust")
            return "Dust"
        if (mode === "red-eye")
            return "Red Eye"
        if (mode === "skin-cleanup")
            return "Skin Cleanup (Heuristic)"
        if (mode === "frequency-smoothing")
            return "Frequency Smoothing"
        return "Retouch"
    }

    function retouchAuditText(retouch) {
        const auditLabel = String(retouch.retouchAuditLabel || "")
        if (auditLabel.length > 0)
            return auditLabel
        const status = retouch.committed ? "applied" : "staged"
        const enabled = retouch.enabled ? "" : "off"
        const pointCount = retouch.brushPoints ? retouch.brushPoints.length : 0
        const pointText = pointCount > 0 ? pointCount.toString() + " pts" : "no strokes"
        const extras = []
        extras.push(status)
        if (enabled.length > 0)
            extras.push(enabled)
        extras.push(pointText)
        if ((retouch.inpaintCandidateCount || 0) > 0)
            extras.push((retouch.inpaintCandidateCount || 0).toString() + " candidates")
        if (retouch.inpaintFallback || false)
            extras.push("fallback")
        return root.retouchModeLabel(retouch.retouchMode) + " · " + extras.join(" · ")
    }

    function maskKindLabel(kindName) {
        const kind = String(kindName || "").trim().toLowerCase()
        if (kind === "linear")
            return "Linear source"
        if (kind === "radial")
            return "Radial source"
        if (kind === "brush")
            return "Brush source"
        if (kind === "object")
            return "Object source"
        if (kind === "subject")
            return "Subject source"
        if (kind === "sky")
            return "Sky heuristic source"
        if (kind === "background")
            return "Background derived source"
        if (kind === "people")
            return "People foreground source"
        if (kind === "face")
            return "Face source unsupported"
        if (kind === "hair")
            return "Hair source unsupported"
        if (kind === "clothing")
            return "Clothing source unsupported"
        if (kind === "polygon")
            return "Polygon source"
        if (kind === "lasso")
            return "Lasso source"
        if (kind === "luminance")
            return "Luminance range"
        if (kind === "color")
            return "Color range"
        if (kind === "depth")
            return "Depth range"
        if (kind.length === 0)
            return "Selection source"
        return kind.charAt(0).toUpperCase() + kind.slice(1) + " source"
    }

    function maskOperationLabel(operationName) {
        const operation = String(operationName || "").trim().toLowerCase()
        if (operation === "subtract")
            return "Subtract"
        if (operation === "intersect")
            return "Intersect"
        if (operation === "exclude")
            return "Exclude"
        if (operation === "replace")
            return "Replace"
        if (operation === "union")
            return "Union"
        return "Add"
    }

    function maskOutlineQualityLabel(tierName) {
        const tier = String(tierName || "hidden").trim().toLowerCase()
        if (tier === "live-vector")
            return "Outline: Live"
        if (tier === "preview-alpha")
            return "Outline: Preview alpha"
        if (tier === "vector-path")
            return "Outline: Vector"
        if (tier === "settled-alpha")
            return "Outline: Alpha"
        if (tier === "unavailable")
            return "Outline: Unavailable"
        return "Outline: Hidden"
    }

    function maskPreviewQualityLabel(tierName) {
        const tier = String(tierName || "hidden").trim().toLowerCase()
        if (tier === "full")
            return "Preview: Full Res"
        if (tier === "high")
            return "Preview: High Res"
        if (tier === "draft")
            return "Preview: Draft"
        if (tier === "outline")
            return "Preview: Outline Only"
        return "Preview: Hidden"
    }

    function maskStateSummary(mask) {
        if (!mask)
            return "No active selection"
        const parts = []
        parts.push(root.maskKindLabel(mask.kind))
        parts.push(root.maskOperationLabel(mask.operation))
        parts.push((mask.invert || mask.selectionInvert) ? "Inverted" : "Normal")
        parts.push(root.maskPreviewQualityLabel(editorPreview.activeMaskPreviewQualityTier))
        parts.push(root.maskOutlineQualityLabel(editorPreview.activeMaskOutlineQualityTier))
        if (!(mask.enabled !== false))
            parts.push("Off")
        if (mask.solo)
            parts.push("Solo")
        if (mask.locked)
            parts.push("Locked")
        return parts.join(" · ")
    }

    function activeMaskStatusText() {
        if (!editorPreview.hasActiveMask)
            return "Choose a tool to begin."
        if (editorPreview.activeMaskToolRole === "retouch")
            return root.retouchAuditText(root.activeMaskData || ({}))
        return root.maskStateSummary(root.activeMaskData)
    }

    function refineStrengths() {
        const featherDefault = editRoot.activeMaskResetFeather()
        return [
            {
                "key": "expand",
                "label": "Shift",
                "value": Math.min(1, Math.abs(editorPreview.activeMaskSelectionExpand) / 100),
                "accent": editorPreview.activeMaskSelectionExpand < 0 ? "#d98962" : root.activeMaskAccent
            },
            {
                "key": "feather",
                "label": "Feather",
                "value": Math.min(1, Math.abs(editorPreview.activeMaskFeather - featherDefault) / 0.95),
                "accent": "#8fb7d8"
            },
            {
                "key": "smooth",
                "label": "Smooth",
                "value": Math.min(1, editorPreview.activeMaskSelectionSmooth / 100),
                "accent": "#72cbb2"
            },
            {
                "key": "fine",
                "label": "Fine",
                "value": Math.min(1, editorPreview.activeMaskSelectionFineEdge / 100),
                "accent": "#c994d8"
            },
            {
                "key": "edge",
                "label": "Contrast",
                "value": Math.min(1, editorPreview.activeMaskSelectionEdgeContrast / 100),
                "accent": "#f2c56b"
            },
            {
                "key": "spill",
                "label": "Spill",
                "value": Math.min(1, editorPreview.activeMaskSelectionDecontaminate / 100),
                "accent": "#d98962"
            }
        ]
    }

    function activeRefineEffectCount() {
        const strengths = root.refineStrengths()
        let count = editorPreview.activeMaskSelectionInverted ? 1 : 0
        for (let index = 0; index < strengths.length; ++index) {
            if (strengths[index].value > 0.01)
                ++count
        }
        return count
    }

    spacing: 10

    MaskSection {
        width: parent.width
        title: "Mask"

        Rectangle {
            Layout.fillWidth: true
            radius: 10
            color: "#161a1e"
            border.width: 0
            border.color: "transparent"
            implicitHeight: statusColumn.implicitHeight + 20

            Column {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                RowLayout {
                    width: parent.width
                    spacing: 6

                    Rectangle {
                        radius: 5
                        color: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole === "retouch"
                               ? Qt.alpha(root.retouchAccent(editorPreview.activeMaskRetouchMode), 0.16)
                               : Qt.alpha(root.activeMaskAccent, 0.16)
                        border.width: 1
                        border.color: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole === "retouch"
                                      ? Qt.alpha(root.retouchAccent(editorPreview.activeMaskRetouchMode), 0.34)
                                      : Qt.alpha(root.activeMaskAccent, 0.34)
                        implicitWidth: modeChip.implicitWidth + 12
                        implicitHeight: 18

                        Label {
                            id: modeChip
                            objectName: "activeMaskModeChip"
                            anchors.centerIn: parent
                            text: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole === "retouch" ? "Retouch" : "Select"
                            color: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole === "retouch"
                                   ? root.retouchAccent(editorPreview.activeMaskRetouchMode)
                                   : root.activeMaskAccent
                            font.family: theme.fontDisplay
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.2
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                Label {
                    objectName: "activeMaskTitleLabel"
                    width: parent.width
                    text: editorPreview.hasActiveMask
                          ? (editorPreview.activeMaskToolRole === "retouch"
                             ? editRoot.activeToolLabel()
                             : "Editing " + editorPreview.activeMaskName)
                          : "No active selection"
                    color: "#f2f4f6"
                    font.family: theme.fontDisplay
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }

                Label {
                    objectName: "activeMaskStatusLabel"
                    width: parent.width
                    text: editRoot.cloneSourcePickArmed
                              ? "Click a clone source."
                              : editRoot.objectPickArmed && editRoot.objectPickMode === "remove"
                                ? "Click or drag a target."
                                : editRoot.objectPickArmed && editRoot.objectPickMode === "selection-subtract"
                                  ? "Click an object to subtract."
                                : editRoot.objectPickArmed
                                  ? "Click an object to select."
                                  : editRoot.polygonDrawArmed
                                    ? "Click points. Double-click to finish."
                                     : editRoot.lassoDrawArmed
                                       ? "Drag on the canvas."
                                       : root.activeMaskStatusText()
                    wrapMode: Text.WordWrap
                    color: "#8f98a2"
                    font.family: theme.fontUi
                    font.pixelSize: 10
                }
            }
        }

        Flow {
            objectName: "normalMaskPreviewModeFlow"
            readonly property string displayContract: root.normalMaskDisplayContract
            Layout.fillWidth: true
            width: parent.width
            spacing: 6

            ActionButton {
                objectName: "maskPreviewOverlayButton"
                text: "Color Overlay"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "overlay"
                enabled: editorPreview.hasAsset
                tooltipText: "Show a colored selection overlay on the image."
                onClicked: editorPreview.maskPreviewMode = "overlay"
            }

            ActionButton {
                objectName: "maskPreviewOutlineButton"
                text: "Outline"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewDisplayMode === "outline"
                enabled: editorPreview.hasAsset
                tooltipText: "Show a solid selection boundary only."
                onClicked: editorPreview.maskPreviewMode = "outline"
            }

            ActionButton {
                objectName: "maskPreviewMarchingAntsButton"
                text: "Marching Ants"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewDisplayMode === "marching-ants"
                enabled: editorPreview.hasAsset
                tooltipText: "Show the animated selection boundary only."
                onClicked: editorPreview.maskPreviewMode = "marching-ants"
            }

            ActionButton {
                objectName: "maskPreviewNoneButton"
                text: "Hidden"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewDisplayMode === "hidden"
                enabled: editorPreview.hasAsset
                tooltipText: "Hide selection preview overlays."
                onClicked: editorPreview.maskPreviewMode = "hidden"
            }
        }

        ActionButton {
            objectName: "maskPreviewDiagnosticsToggleButton"
            readonly property string diagnosticsMenuContract: root.developerDiagnosticsMenuContract
            width: parent.width
            text: root.diagnosticMaskPreviewModesVisible ? "Hide Matte Diagnostics" : "Matte Diagnostics"
            compact: true
            accent: root.activeMaskAccent
            tone: root.diagnosticMaskPreviewModesVisible ? "primary" : "secondary"
            active: root.diagnosticMaskPreviewModesVisible
            visible: editRoot.developerDiagnosticsEnabled === true
            enabled: editorPreview.hasAsset && editRoot.developerDiagnosticsEnabled === true
            tooltipText: "Show deliberate matte inspection modes separately from the normal selection overlay."
            onClicked: root.diagnosticMaskPreviewModesVisible =
                           editRoot.developerDiagnosticsEnabled === true &&
                           !root.diagnosticMaskPreviewModesVisible
        }

        Flow {
            objectName: "diagnosticMaskPreviewModeFlow"
            readonly property string displayContract: root.diagnosticMaskDisplayContract
            readonly property string diagnosticsMenuContract: root.developerDiagnosticsMenuContract
            visible: editRoot.developerDiagnosticsEnabled === true &&
                     root.diagnosticMaskPreviewModesVisible
            Layout.fillWidth: true
            width: parent.width
            spacing: 6

            ActionButton {
                objectName: "maskPreviewColorOnBwButton"
                text: "Color on B&W"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "color-on-bw"
                enabled: editorPreview.hasAsset
                tooltipText: "Show the colored selection overlay against a muted black-and-white image context."
                onClicked: editorPreview.maskPreviewMode = "color-on-bw"
            }

            ActionButton {
                objectName: "maskPreviewImageOnBwButton"
                text: "Image on B&W"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "image-on-bw"
                enabled: editorPreview.hasAsset
                tooltipText: "Inspect selected-image coverage against a muted black-and-white image context."
                onClicked: editorPreview.maskPreviewMode = "image-on-bw"
            }

            ActionButton {
                objectName: "maskPreviewMatteButton"
                text: "Grayscale Alpha"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewDisplayMode === "grayscale-alpha"
                enabled: editorPreview.hasAsset
                tooltipText: "Show a neutral black-and-white matte without the tinted overlay."
                onClicked: editorPreview.maskPreviewMode = "grayscale-alpha"
            }

            ActionButton {
                objectName: "maskPreviewOnBlackButton"
                text: "Isolated Mask"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewDisplayMode === "isolated-mask"
                enabled: editorPreview.hasAsset
                tooltipText: "Inspect the selected pixels on a black backing."
                onClicked: editorPreview.maskPreviewMode = "isolated-mask"
            }

            ActionButton {
                objectName: "maskPreviewOnWhiteButton"
                text: "On White"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "on-white"
                enabled: editorPreview.hasAsset
                tooltipText: "Inspect the selection overlay on a white backing."
                onClicked: editorPreview.maskPreviewMode = "on-white"
            }

            ActionButton {
                objectName: "maskPreviewEdgeButton"
                visible: false
                text: "Edge"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "edge"
                enabled: editorPreview.hasAsset
                tooltipText: "Show only semi-transparent selection edges."
                onClicked: editorPreview.maskPreviewMode = "edge"
            }

            ActionButton {
                objectName: "maskPreviewRefinementEdgeButton"
                visible: false
                text: "Refine Edge"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: false
                enabled: false
                tooltipText: "Show only matte pixels changed by selection refinement controls."
            }

            ActionButton {
                objectName: "maskPreviewAffectedButton"
                visible: false
                text: "Affected"
                compact: true
                accent: root.activeMaskAccent
                tone: active ? "primary" : "secondary"
                active: editorPreview.maskPreviewMode === "affected"
                enabled: editorPreview.hasAsset
                tooltipText: "Highlight pixels affected by the active selection."
                onClicked: editorPreview.maskPreviewMode = "affected"
            }
        }

        InspectorSlider {
            objectName: "maskPreviewOverlayOpacitySlider"
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            readonly property string terminologyContract: root.maskLayerParameterTerminologyContract
            readonly property string parameterRole: "overlay-opacity"
            width: parent.width
            label: "OVERLAY OPACITY"
            from: 0
            to: 100
            stepSize: 1
            defaultValue: 100
            value: Math.round(editorPreview.maskPreviewOverlayOpacity * 100)
            valueText: Math.round(editorPreview.maskPreviewOverlayOpacity * 100).toString() + "%"
            accent: root.maskPreviewOverlayResolvedColor
            onValueDragged: function(value) { editorPreview.maskPreviewOverlayOpacity = value / 100 }
            onResetRequested: editorPreview.maskPreviewOverlayOpacity = 1.0
        }

        RowLayout {
            width: parent.width
            spacing: 8

            Rectangle {
                objectName: "maskPreviewOverlayColorSwatch"
                Layout.preferredWidth: 32
                Layout.preferredHeight: 24
                radius: 3
                color: root.maskPreviewOverlayResolvedColor
                border.width: 1
                border.color: theme.alpha(theme.polar, 0.36)

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: maskPreviewOverlayColorDialog.open()
                }
            }

            ActionButton {
                objectName: "maskPreviewOverlayColorButton"
                Layout.fillWidth: true
                text: "Overlay Color"
                compact: true
                accent: root.maskPreviewOverlayResolvedColor
                tooltipText: "Pick the selection preview overlay color."
                onClicked: maskPreviewOverlayColorDialog.open()
            }

            ActionButton {
                objectName: "maskPreviewOverlayColorResetButton"
                text: "Auto"
                compact: true
                accent: root.activeMaskAccent
                tooltipText: "Use automatic operation colors for selection preview overlays."
                onClicked: editorPreview.maskPreviewOverlayColor = ""
            }

            ActionButton {
                objectName: "maskPreviewRubylithOverlayPresetButton"
                readonly property string rubylithOverlayPresetContract: root.rubylithOverlayPresetContract
                text: "Rubylith"
                compact: true
                accent: "#ff2f5f"
                tone: String(editorPreview.maskPreviewOverlayColor || "").toLowerCase() === "#ff2f5f"
                      && editorPreview.maskPreviewDisplayMode === "overlay" ? "primary" : "secondary"
                active: String(editorPreview.maskPreviewOverlayColor || "").toLowerCase() === "#ff2f5f"
                        && editorPreview.maskPreviewDisplayMode === "overlay"
                enabled: editorPreview.hasAsset
                tooltipText: "Use a classic single-color red selection overlay."
                onClicked: {
                    editorPreview.maskPreviewMode = "overlay"
                    editorPreview.maskPreviewOverlayColor = "#ff2f5f"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ActionButton {
                objectName: "maskPreviewShowActiveOnlyButton"
                Layout.fillWidth: true
                text: "Active Only"
                compact: true
                accent: root.maskPreviewOverlayResolvedColor
                tone: !editorPreview.maskPreviewShowAll ? "primary" : "secondary"
                active: !editorPreview.maskPreviewShowAll
                enabled: editorPreview.hasAsset
                tooltipText: "Show only the active selection mask preview."
                onClicked: editorPreview.maskPreviewShowAll = false
            }

            ActionButton {
                objectName: "maskPreviewShowAllButton"
                Layout.fillWidth: true
                text: "Show All Masks"
                compact: true
                accent: root.maskPreviewOverlayResolvedColor
                tone: editorPreview.maskPreviewShowAll ? "primary" : "secondary"
                active: editorPreview.maskPreviewShowAll
                enabled: editorPreview.hasAsset
                tooltipText: "Show every enabled selection mask instead of only the active mask."
                onClicked: editorPreview.maskPreviewShowAll = !editorPreview.maskPreviewShowAll
            }

            ActionButton {
                objectName: "maskPreviewShowInvertedButton"
                Layout.fillWidth: true
                text: editorPreview.activeMaskSelectionInverted ? "Inverted" : "Normal"
                compact: true
                accent: editorPreview.activeMaskSelectionInverted ? "#d98962" : root.maskPreviewOverlayResolvedColor
                tone: editorPreview.activeMaskSelectionInverted ? "primary" : "secondary"
                active: editorPreview.activeMaskSelectionInverted
                enabled: editorPreview.hasActiveMask &&
                         editorPreview.activeMaskToolRole !== "retouch"
                tooltipText: editorPreview.activeMaskSelectionInverted
                             ? "Show and edit the normal selected region."
                             : "Show and edit the inverse selected region."
                onClicked: editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.hasActiveMask ? "Deselect" : "Exit Selection"
                compact: true
                enabled: editorPreview.hasActiveMask ||
                         editRoot.objectPickArmed ||
                         editRoot.polygonDrawArmed ||
                         editRoot.lassoDrawArmed ||
                         editRoot.cloneSourcePickArmed
                onClicked: editRoot.deselectMaskEditing()
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Delete Active"
                tooltipText: editorPreview.activeMaskToolRole === "retouch"
                             ? "Remove the active retouch recipe entry; the original file is unchanged."
                             : "Remove the active selection recipe entry; the original file is unchanged."
                compact: true
                accent: "#d36a5e"
                enabled: editorPreview.hasActiveMask
                onClicked: editorPreview.removeActiveMask()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: editorPreview.hasActiveMask &&
                     editorPreview.activeMaskToolRole === "retouch" &&
                     !editorPreview.activeMaskCommitted
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskRetouchMode === "remove"
                      ? "Apply Remove"
                      : (editorPreview.activeMaskRetouchMode === "heal"
                         ? "Apply Heal"
                         : (editorPreview.activeMaskRetouchMode === "patch"
                            ? "Apply Patch"
                            : (editorPreview.activeMaskRetouchMode === "dust"
                               ? "Apply Dust"
                               : (editorPreview.activeMaskRetouchMode === "red-eye"
                                  ? "Apply Red Eye"
                                  : "Apply Clone"))))
                compact: true
                accent: root.retouchAccent(editorPreview.activeMaskRetouchMode)
                tone: "primary"
                tooltipText: "Apply this retouch patch to the edit recipe; the original file is unchanged."
                onClicked: editorPreview.applyActiveRetouch()
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Discard"
                compact: true
                tooltipText: "Discard the staged retouch patch from the recipe; the original file is unchanged."
                onClicked: {
                    editRoot.clearTransientTools()
                    editorPreview.cancelActiveRetouch()
                }
            }
        }

        Label {
            visible: !editorPreview.subjectMaskAvailable
            Layout.fillWidth: true
            text: "Vision Object Select requires macOS 14 or later. " +
                  root.smartSelectionRuntimeLabel() + " remains available for rough local selections."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                radius: 6
                color: "#121920"
                border.width: 1
                border.color: editorPreview.aiSmartSelectionAvailable ? "#437a68" : "#6f5736"

                Label {
                    objectName: "maskSelectionRuntimeChip"
                    anchors.fill: parent
                    anchors.margins: 6
                    text: "Selection · " + root.smartSelectionRuntimeLabel()
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 8
                    elide: Text.ElideNone
                    verticalAlignment: Text.AlignVCenter
                    color: editorPreview.aiSmartSelectionAvailable ? "#9adfca" : "#d9aa70"
                    font.family: theme.fontUi
                    font.pixelSize: 10
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                radius: 6
                color: "#121920"
                border.width: 1
                border.color: editorPreview.aiInpaintAvailable ? "#437a68" : "#6f5736"

                Label {
                    objectName: "maskInpaintRuntimeChip"
                    anchors.fill: parent
                    anchors.margins: 6
                    text: "Retouch · " + root.inpaintRuntimeLabel()
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 8
                    elide: Text.ElideNone
                    verticalAlignment: Text.AlignVCenter
                    color: editorPreview.aiInpaintAvailable ? "#9adfca" : "#d9aa70"
                    font.family: theme.fontUi
                    font.pixelSize: 10
                }
            }
        }

        ActionButton {
            objectName: "maskRuntimeDiagnosticsToggleButton"
            Layout.fillWidth: true
            text: root.runtimeDiagnosticsExpanded ? "Hide Runtime Details" : "Runtime Details"
            iconName: "menu"
            compact: true
            tone: "secondary"
            active: root.runtimeDiagnosticsExpanded
            visible: editRoot.developerDiagnosticsEnabled === true
            enabled: editRoot.developerDiagnosticsEnabled === true
            tooltipText: "Show local model status and diagnostics without making debug text part of normal editing."
            onClicked: root.runtimeDiagnosticsExpanded =
                           editRoot.developerDiagnosticsEnabled === true &&
                           !root.runtimeDiagnosticsExpanded
        }

        Label {
            objectName: "maskRuntimeDiagnosticsLabel"
            Layout.fillWidth: true
            visible: editRoot.developerDiagnosticsEnabled === true &&
                     root.runtimeDiagnosticsExpanded
            readonly property string visibilityContract: root.runtimeDiagnosticsVisibilityContract
            readonly property string runtimeDiagnosticsSignature: root.runtimeDiagnosticsSignature()
            text: "Selection: " + root.smartSelectionRuntimeLabel() +
                  " · Retouch: " + root.inpaintRuntimeLabel() +
                  "\n" + editorPreview.aiModelStatus +
                  "\n" + String(editorPreview.aiModelDiagnostics || "")
            wrapMode: Text.WordWrap
            color: editorPreview.aiSmartSelectionAvailable || editorPreview.aiInpaintAvailable
                   ? "#72cbb2"
                   : "#d98962"
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MaskSection {
        width: parent.width
        title: "Masks"
        subtitle: "Each mask contains ordered Components that add, subtract, or refine its matte."
        expandable: true
        expanded: true

        Column {
            width: parent.width
            spacing: 8

            TextField {
                objectName: "maskSearchField"
                width: parent.width
                text: root.maskSearchQuery
                placeholderText: "Search masks, tools, controls"
                selectByMouse: true
                font.family: theme.fontUi
                font.pixelSize: 12
                color: "#f2f4f6"
                placeholderTextColor: "#6f7882"
                onTextChanged: root.maskSearchQuery = text
                background: Rectangle {
                    radius: 8
                    color: "#11161a"
                    border.width: 1
                    border.color: root.maskSearchQuery.trim().length > 0 ? Qt.alpha(root.activeMaskAccent, 0.50) : "#2a3138"
                }
            }

            Rectangle {
                objectName: "maskSearchResultsPanel"
                width: parent.width
                visible: root.maskSearchText().length > 0
                implicitHeight: visible ? maskSearchResultsColumn.implicitHeight + 12 : 0
                radius: 8
                color: "#11161a"
                border.width: 1
                border.color: "#2a3138"

                Column {
                    id: maskSearchResultsColumn
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 6

                    Repeater {
                        model: root.maskSearchResults()

                        delegate: ActionButton {
                            required property var modelData
                            objectName: "maskSearchResultButton"
                            width: parent.width
                            text: modelData.category + " · " + modelData.label
                            iconName: modelData.category === "Tool" ? "local-adjust" : (modelData.category === "Mask" ? "masks" : "density")
                            compact: true
                            tooltipText: modelData.detail
                            onClicked: root.activateMaskSearchResult(modelData)
                        }
                    }

                    Label {
                        width: parent.width
                        visible: root.maskSearchResults().length === 0
                        text: "No matching mask tools, masks, or local controls."
                        color: "#8f98a2"
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Repeater {
                model: editorPreview.maskSets

                delegate: Rectangle {
                    id: maskSetDelegate
                    objectName: "maskInspectorSetFocusRow"
                    required property var modelData

                    readonly property bool isActiveSet: {
                        for (let sourceIndex = 0; sourceIndex < modelData.sources.length; ++sourceIndex) {
                            if (modelData.sources[sourceIndex].flatIndex === editorPreview.activeMaskIndex)
                                return true
                        }
                        return false
                    }
                    readonly property var firstSource: modelData.sources.length > 0 ? modelData.sources[0] : null
                    readonly property color setColor: firstSource ? editRoot.maskAccent(firstSource) : "#8ab8d8"

                    width: parent.width
                    visible: root.maskSetMatchesSearch(modelData)
                    radius: 10
                    color: isActiveSet ? Qt.alpha(setColor, 0.12) : "#171b1f"
                    border.width: 1
                    border.color: activeFocus ? "#8ab8d8" : (isActiveSet ? Qt.alpha(setColor, 0.45) : "#2d3339")
                    implicitHeight: setColumn.implicitHeight + 20
                    opacity: modelData.enabled ? 1.0 : 0.55
                    activeFocusOnTab: true
                    Keys.onReturnPressed: if (firstSource) editorPreview.activeMaskIndex = firstSource.flatIndex
                    Keys.onEnterPressed: if (firstSource) editorPreview.activeMaskIndex = firstSource.flatIndex
                    Keys.onSpacePressed: if (firstSource) editorPreview.activeMaskIndex = firstSource.flatIndex

                    Column {
                        id: setColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        RowLayout {
                            id: setHeader
                            width: parent.width
                            spacing: 8

                            TapHandler {
                                onTapped: {
                                    if (firstSource)
                                        editorPreview.activeMaskIndex = firstSource.flatIndex
                                }
                            }

                            Rectangle {
                                objectName: "maskSetThumbnail"
                                Layout.preferredWidth: 44
                                Layout.preferredHeight: 30
                                radius: 5
                                color: "#090c0f"
                                border.width: 1
                                border.color: Qt.alpha(setColor, 0.44)
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    source: modelData.thumbnailSource || ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: false
                                    visible: source.toString().length > 0
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.bottom: parent.bottom
                                    anchors.leftMargin: 4
                                    anchors.bottomMargin: 4
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: setColor
                                    border.width: 1
                                    border.color: "#0c1014"
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    color: "#f2f4f6"
                                    font.family: theme.fontUi
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    maximumLineCount: 2
                                    elide: Text.ElideNone
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.sourceCount === 1
                                          ? "1 component"
                                          : modelData.sourceCount.toString() + " components"
                                    color: "#8f98a2"
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    fontSizeMode: Text.HorizontalFit
                                    minimumPixelSize: 8
                                    elide: Text.ElideNone
                                }
                            }

                            ActionButton {
                                iconName: modelData.enabled ? "visible" : "hidden"
                                showText: false
                                compact: true
                                active: modelData.enabled
                                accent: setColor
                                tooltipText: modelData.enabled ? "Disable selection set" : "Enable selection set"
                                enabled: firstSource !== null
                                onClicked: {
                                    const newEnabled = !modelData.enabled
                                    for (let si = 0; si < modelData.sources.length; ++si) {
                                        editorPreview.activeMaskIndex = modelData.sources[si].flatIndex
                                        editorPreview.activeMaskEnabled = newEnabled
                                    }
                                    if (firstSource)
                                        editorPreview.activeMaskIndex = firstSource.flatIndex
                                }
                            }
                        }

                        Label {
                            objectName: "maskComponentsHeaderLabel"
                            width: parent.width
                            text: "Components"
                            color: "#aeb7c1"
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 8
                            elide: Text.ElideNone
                            visible: modelData.sourceCount > 0
                        }

                        Repeater {
                            model: root.maskTreeSources(modelData.sources).filter(root.maskMatchesSearch)

                            delegate: Rectangle {
                                id: maskSourceDelegate
                                objectName: "maskInspectorSourceFocusRow"
                                required property var modelData

                                x: Math.min(40, modelData.depth * 14)
                                width: parent.width - x
                                radius: 8
                                color: modelData.flatIndex === editorPreview.activeMaskIndex
                                       ? "#21272d"
                                       : "#14181c"
                                border.width: 1
                                border.color: activeFocus
                                              ? "#8ab8d8"
                                              : (modelData.flatIndex === editorPreview.activeMaskIndex
                                                 ? editRoot.maskAccent(modelData)
                                                 : "#242b31")
                                implicitHeight: 52
                                opacity: modelData.enabled ? 1.0 : 0.6
                                activeFocusOnTab: true
                                Keys.onReturnPressed: editorPreview.activeMaskIndex = modelData.flatIndex
                                Keys.onEnterPressed: editorPreview.activeMaskIndex = modelData.flatIndex
                                Keys.onSpacePressed: editorPreview.activeMaskIndex = modelData.flatIndex
                                Drag.active: maskSourceDragHandler.active
                                Drag.keys: ["mask-source"]
                                Drag.supportedActions: Qt.MoveAction
                                Drag.mimeData: {
                                    "application/x-photo-editor-mask-source-index": String(modelData.flatIndex)
                                }

                                DragHandler {
                                    id: maskSourceDragHandler
                                    objectName: "maskSourceDragHandler"
                                    target: null
                                    onActiveChanged: {
                                        if (active)
                                            editorPreview.activeMaskIndex = modelData.flatIndex
                                    }
                                }

                                DropArea {
                                    objectName: "maskSourceReorderDropArea"
                                    anchors.fill: parent
                                    keys: ["mask-source"]
                                    onDropped: function(drop) {
                                        const sourceIndex = Number(drop.getDataAsString("application/x-photo-editor-mask-source-index"))
                                        if (Number.isFinite(sourceIndex) &&
                                                editorPreview.moveMaskSource(sourceIndex, maskSetDelegate.modelData.id, index))
                                            drop.acceptProposedAction()
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: function(mouse) {
                                        editorPreview.activeMaskIndex = modelData.flatIndex
                                        if (mouse.button === Qt.RightButton)
                                            maskInspectorSourceContextMenu.popup()
                                    }
                                }

                                AppMenu {
                                    id: maskInspectorSourceContextMenu
                                    objectName: "maskInspectorSourceContextMenu"
                                    accent: root.activeMaskAccent

                                    AppMenuItem {
                                        objectName: "maskInspectorSourceContextSelectItem"
                                        text: "Select"
                                        iconName: "select"
                                        accent: maskInspectorSourceContextMenu.accent
                                        onTriggered: editorPreview.activeMaskIndex = modelData.flatIndex
                                    }

                                    AppMenuItem {
                                        objectName: "maskInspectorSourceContextRenameItem"
                                        text: "Rename"
                                        iconName: "edit"
                                        accent: maskInspectorSourceContextMenu.accent
                                        onTriggered: root.renameMaskRow(modelData.flatIndex)
                                    }

                                    AppMenuItem {
                                        objectName: "maskInspectorSourceContextEnableItem"
                                        text: modelData.enabled ? "Disable" : "Enable"
                                        iconName: modelData.enabled ? "hidden" : "visible"
                                        accent: maskInspectorSourceContextMenu.accent
                                        enabled: !modelData.locked
                                        onTriggered: root.toggleMaskRowEnabled(modelData.flatIndex, modelData.enabled)
                                    }

                                    AppMenuItem {
                                        objectName: "maskInspectorSourceContextSoloItem"
                                        text: modelData.solo ? "Clear Solo" : "Solo"
                                        iconName: "star"
                                        accent: maskInspectorSourceContextMenu.accent
                                        enabled: !modelData.locked
                                        onTriggered: root.toggleMaskRowSolo(modelData.flatIndex, modelData.solo)
                                    }

                                    AppMenuItem {
                                        objectName: "maskInspectorSourceContextLockItem"
                                        text: modelData.locked ? "Unlock" : "Lock"
                                        iconName: "lock"
                                        accent: maskInspectorSourceContextMenu.accent
                                        onTriggered: root.toggleMaskRowLocked(modelData.flatIndex, modelData.locked)
                                    }

                                    MenuSeparator {}

                                    AppMenuItem {
                                        objectName: "maskInspectorSourceContextMoveUpItem"
                                        text: "Move Up"
                                        iconName: "move-up"
                                        accent: maskInspectorSourceContextMenu.accent
                                        enabled: modelData.flatIndex > 0
                                        onTriggered: root.moveMaskRow(modelData.flatIndex, -1)
                                    }

                                    AppMenuItem {
                                        objectName: "maskInspectorSourceContextMoveDownItem"
                                        text: "Move Down"
                                        iconName: "move-down"
                                        accent: maskInspectorSourceContextMenu.accent
                                        enabled: modelData.flatIndex < editorPreview.maskCount - 1
                                        onTriggered: root.moveMaskRow(modelData.flatIndex, 1)
                                    }

                                    AppMenuItem {
                                        objectName: "maskInspectorSourceContextDuplicateItem"
                                        text: "Duplicate"
                                        iconName: "plus"
                                        accent: maskInspectorSourceContextMenu.accent
                                        onTriggered: root.duplicateMaskRow(modelData.flatIndex)
                                    }

                                    AppMenuItem {
                                        objectName: "maskInspectorSourceContextDeleteItem"
                                        text: "Delete"
                                        iconName: "delete"
                                        danger: true
                                        accent: maskInspectorSourceContextMenu.accent
                                        onTriggered: root.deleteMaskRow(modelData.flatIndex)
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 8

                                    Rectangle {
                                        objectName: "maskSourceThumbnail"
                                        Layout.preferredWidth: 34
                                        Layout.preferredHeight: 24
                                        radius: 5
                                        color: "#090c0f"
                                        border.width: 1
                                        border.color: Qt.alpha(editRoot.maskAccent(modelData), 0.42)
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            source: modelData.thumbnailSource || ""
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: false
                                            visible: source.toString().length > 0
                                        }

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.bottom: parent.bottom
                                            anchors.leftMargin: 3
                                            anchors.bottomMargin: 3
                                            width: 7
                                            height: 7
                                            radius: 4
                                            color: editRoot.maskAccent(modelData)
                                            border.width: 1
                                            border.color: "#0c1014"
                                        }
                                    }

                                    Rectangle {
                                        visible: modelData.depth > 0
                                        Layout.preferredWidth: 12
                                        Layout.preferredHeight: 1
                                        color: "#46505a"
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            color: "#f2f4f6"
                                            font.family: theme.fontUi
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideNone
                                        }

                                        Label {
                                            objectName: "maskSourceSummaryLabel"
                                            Layout.fillWidth: true
                                            text: root.maskStateSummary(modelData)
                                            color: "#8f98a2"
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideNone
                                        }

                                        Label {
                                            objectName: "maskSourceDependencyLabel"
                                            Layout.fillWidth: true
                                            visible: String(modelData.dependencyLabel || "").length > 0
                                            text: String(modelData.dependencyLabel || "")
                                            color: editRoot.maskAccent(modelData)
                                            font.family: theme.fontUi
                                            font.pixelSize: 10
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            maximumLineCount: 2
                                            elide: Text.ElideNone
                                        }
                                    }

                                    ActionButton {
                                        text: modelData.enabled ? "On" : "Off"
                                        compact: true
                                        active: modelData.enabled
                                        accent: modelData.enabled ? editRoot.maskAccent(modelData) : "#d98962"
                                        enabled: !modelData.locked
                                        tooltipText: modelData.enabled ? "Disable this source" : "Enable this source"
                                        onClicked: root.toggleMaskRowEnabled(modelData.flatIndex, modelData.enabled)
                                    }

                                    ActionButton {
                                        text: "Solo"
                                        compact: true
                                        active: modelData.solo
                                        enabled: !modelData.locked
                                        tooltipText: modelData.solo ? "Clear solo" : "Solo this selection"
                                        onClicked: root.toggleMaskRowSolo(modelData.flatIndex, modelData.solo)
                                    }

                                    ActionButton {
                                        text: modelData.locked ? "Locked" : "Lock"
                                        compact: true
                                        active: modelData.locked
                                        accent: modelData.locked ? "#d98962" : editRoot.maskAccent(modelData)
                                        tooltipText: modelData.locked ? "Unlock this selection" : "Lock this selection"
                                        onClicked: root.toggleMaskRowLocked(modelData.flatIndex, modelData.locked)
                                    }

                                    ActionButton {
                                        objectName: "maskSourceInlineDeleteButton"
                                        text: "Delete"
                                        iconName: "delete"
                                        tone: "danger"
                                        compact: true
                                        tooltipText: "Delete this mask component."
                                        onClicked: root.deleteMaskRow(modelData.flatIndex)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 6
                visible: editorPreview.reusableMaskOutputs().length > 0

                Label {
                    width: parent.width
                    text: "Reusable Outputs"
                    color: "#8f98a2"
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.8
                }

                Flow {
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: editorPreview.reusableMaskOutputs()

                        delegate: FilterChip {
                            required property var modelData

                            text: modelData.name + " -> " + modelData.id
                            active: true
                            enabled: false
                        }
                    }
                }
            }

            Rectangle {
                objectName: "maskInspectorEmptyStateGuidance"
                visible: editorPreview.maskSetCount === 0
                width: parent.width
                implicitHeight: emptyMaskSets.implicitHeight + 24
                radius: 8
                color: "#15191e"
                border.width: 1
                border.color: "#232830"

                Column {
                    id: emptyMaskSets
                    anchors.centerIn: parent
                    width: parent.width - 24
                    spacing: 6

                    Label {
                        width: parent.width
                        text: "No masks yet"
                        horizontalAlignment: Text.AlignHCenter
                        color: "#8f98a2"
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }

                    Label {
                        width: parent.width
                        text: "Create a mask with " + root.smartSelectionRuntimeLabel() +
                              ", Pick Object, Brush, Lasso, Polygon, a range control, or Alpha Image."
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: "#6b7580"
                        font.family: theme.fontUi
                        font.pixelSize: 11
                    }
                }
            }
        }

        RowLayout {
            width: parent.width
            visible: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Deselect"
                compact: true
                onClicked: editRoot.deselectMaskEditing()
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Move Up"
                compact: true
                onClicked: editorPreview.moveActiveMask(-1)
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Move Down"
                compact: true
                onClicked: editorPreview.moveActiveMask(1)
            }

            ActionButton {
                objectName: "activeMaskDuplicateButton"
                Layout.fillWidth: true
                text: "Duplicate"
                compact: true
                onClicked: editorPreview.duplicateActiveMask()
            }

            ConfirmActionButton {
                objectName: "activeMaskDeleteButton"
                Layout.fillWidth: true
                text: "Delete"
                compact: true
                helperText: "Remove this selection recipe entry; the original file is unchanged."
                onConfirmed: editorPreview.removeActiveMask()
            }
        }
    }

    MaskSection {
        width: parent.width
        title: "Retouch"
        subtitle: "Heal, clone, skin, red-eye, dust, and remove edits stay separate from selection masks."
        expandable: true
        expanded: editorPreview.activeMaskToolRole === "retouch"

        Column {
            width: parent.width
            spacing: 8

            Repeater {
                model: editorPreview.retouchStack

                delegate: Rectangle {
                    objectName: "maskInspectorRetouchFocusRow"
                    required property var modelData

                    width: parent.width
                    radius: 10
                    color: modelData.flatIndex === editorPreview.activeMaskIndex ? "#21272d" : "#171b1f"
                    border.width: 1
                    border.color: activeFocus
                                  ? "#8ab8d8"
                                  : (modelData.flatIndex === editorPreview.activeMaskIndex
                                     ? editRoot.maskAccent(modelData)
                                     : "#2d3339")
                    implicitHeight: retouchRowLayout.implicitHeight + 20
                    opacity: modelData.enabled ? 1.0 : 0.55
                    activeFocusOnTab: true
                    Keys.onReturnPressed: editorPreview.activeMaskIndex = modelData.flatIndex
                    Keys.onEnterPressed: editorPreview.activeMaskIndex = modelData.flatIndex
                    Keys.onSpacePressed: editorPreview.activeMaskIndex = modelData.flatIndex

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function(mouse) {
                            editorPreview.activeMaskIndex = modelData.flatIndex
                            if (mouse.button === Qt.RightButton)
                                maskInspectorRetouchContextMenu.popup()
                        }
                    }

                    AppMenu {
                        id: maskInspectorRetouchContextMenu
                        objectName: "maskInspectorRetouchContextMenu"
                        accent: root.activeMaskAccent

                        AppMenuItem {
                            objectName: "maskInspectorRetouchContextSelectItem"
                            text: "Select"
                            iconName: "select"
                            accent: maskInspectorRetouchContextMenu.accent
                            onTriggered: editorPreview.activeMaskIndex = modelData.flatIndex
                        }

                        AppMenuItem {
                            objectName: "maskInspectorRetouchContextApplyItem"
                            text: "Apply"
                            iconName: "check"
                            accent: maskInspectorRetouchContextMenu.accent
                            visible: !modelData.committed
                            onTriggered: root.applyRetouchRow(modelData.flatIndex)
                        }

                        AppMenuItem {
                            objectName: "maskInspectorRetouchContextCancelItem"
                            text: "Cancel"
                            iconName: "close"
                            accent: maskInspectorRetouchContextMenu.accent
                            visible: !modelData.committed
                            onTriggered: root.cancelRetouchRow(modelData.flatIndex)
                        }

                        AppMenuItem {
                            objectName: "maskInspectorRetouchContextRenameItem"
                            text: "Rename"
                            iconName: "edit"
                            accent: maskInspectorRetouchContextMenu.accent
                            onTriggered: root.renameMaskRow(modelData.flatIndex)
                        }

                        AppMenuItem {
                            objectName: "maskInspectorRetouchContextEnableItem"
                            text: modelData.enabled ? "Disable" : "Enable"
                            iconName: modelData.enabled ? "hidden" : "visible"
                            accent: maskInspectorRetouchContextMenu.accent
                            onTriggered: root.toggleMaskRowEnabled(modelData.flatIndex, modelData.enabled)
                        }

                        MenuSeparator {}

                        AppMenuItem {
                            objectName: "maskInspectorRetouchContextMoveUpItem"
                            text: "Move Up"
                            iconName: "move-up"
                            accent: maskInspectorRetouchContextMenu.accent
                            enabled: modelData.flatIndex > 0
                            onTriggered: root.moveMaskRow(modelData.flatIndex, -1)
                        }

                        AppMenuItem {
                            objectName: "maskInspectorRetouchContextMoveDownItem"
                            text: "Move Down"
                            iconName: "move-down"
                            accent: maskInspectorRetouchContextMenu.accent
                            enabled: modelData.flatIndex < editorPreview.maskCount - 1
                            onTriggered: root.moveMaskRow(modelData.flatIndex, 1)
                        }

                        AppMenuItem {
                            objectName: "maskInspectorRetouchContextDeleteItem"
                            text: "Delete"
                            iconName: "delete"
                            danger: true
                            accent: maskInspectorRetouchContextMenu.accent
                            onTriggered: root.deleteMaskRow(modelData.flatIndex)
                        }
                    }

                    ColumnLayout {
                        id: retouchRowLayout
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.preferredWidth: 10
                                Layout.preferredHeight: 10
                                radius: 5
                                color: editRoot.maskAccent(modelData)
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    color: "#f2f4f6"
                                    font.family: theme.fontUi
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    maximumLineCount: 2
                                    elide: Text.ElideNone
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.retouchMode.length > 0
                                          ? root.retouchAuditText(modelData)
                                          : "retouch"
                                    color: "#8f98a2"
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    fontSizeMode: Text.HorizontalFit
                                    minimumPixelSize: 8
                                    elide: Text.ElideNone
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            spacing: 4

                            ActionButton {
                                objectName: "retouchListSelectButton"
                                text: "Select"
                                compact: true
                                active: modelData.flatIndex === editorPreview.activeMaskIndex
                                accent: editRoot.maskAccent(modelData)
                                onClicked: editorPreview.activeMaskIndex = modelData.flatIndex
                            }

                            ActionButton {
                                objectName: "retouchListApplyButton"
                                text: "Apply"
                                compact: true
                                visible: !modelData.committed
                                accent: editRoot.maskAccent(modelData)
                                tooltipText: "Apply this retouch patch to the edit recipe; the original file is unchanged."
                                onClicked: root.applyRetouchRow(modelData.flatIndex)
                            }

                            ActionButton {
                                objectName: "retouchListCancelButton"
                                text: "Cancel"
                                compact: true
                                visible: !modelData.committed
                                accent: "#d98962"
                                tooltipText: "Discard the staged retouch patch from the recipe; the original file is unchanged."
                                onClicked: root.cancelRetouchRow(modelData.flatIndex)
                            }

                            ActionButton {
                                objectName: "retouchListRenameButton"
                                text: "Rename"
                                compact: true
                                onClicked: root.renameMaskRow(modelData.flatIndex)
                            }

                            ActionButton {
                                objectName: "retouchListEnableButton"
                                text: modelData.enabled ? "Off" : "On"
                                compact: true
                                accent: modelData.enabled ? "#d98962" : editRoot.maskAccent(modelData)
                                onClicked: root.toggleMaskRowEnabled(modelData.flatIndex, modelData.enabled)
                            }

                            ActionButton {
                                objectName: "retouchListMoveUpButton"
                                text: "Up"
                                compact: true
                                enabled: modelData.flatIndex > 0
                                onClicked: root.moveMaskRow(modelData.flatIndex, -1)
                            }

                            ActionButton {
                                objectName: "retouchListMoveDownButton"
                                text: "Down"
                                compact: true
                                enabled: modelData.flatIndex < editorPreview.maskCount - 1
                                onClicked: root.moveMaskRow(modelData.flatIndex, 1)
                            }

                            ActionButton {
                                objectName: "retouchListDeleteButton"
                                text: "Delete"
                                compact: true
                                accent: "#d36a5e"
                                tooltipText: "Remove this retouch patch recipe entry; the original file is unchanged."
                                onClicked: root.deleteMaskRow(modelData.flatIndex)
                            }
                        }
                    }
                }
            }

            Label {
                visible: editorPreview.retouchCount === 0
                width: parent.width
                text: "No retouch strokes yet."
                wrapMode: Text.WordWrap
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 12
            }

            RowLayout {
                width: parent.width
                spacing: 6
                visible: editorPreview.retouchCount > 0

                ActionButton {
                    Layout.fillWidth: false
                    text: "Before"
                    iconName: "compare"
                    showText: false
                    tooltipText: "Show retouch before"
                    Accessible.name: "Show retouch before"
                    compact: true
                    accent: "#8fb7d8"
                    tone: active ? "primary" : "secondary"
                    active: editorPreview.retouchBeforeAfterEnabled
                    onClicked: editorPreview.retouchBeforeAfterEnabled = true
                }

                ActionButton {
                    Layout.fillWidth: false
                    text: "After"
                    iconName: "compare"
                    showText: false
                    tooltipText: "Show retouch after"
                    Accessible.name: "Show retouch after"
                    compact: true
                    accent: "#8fb7d8"
                    tone: active ? "primary" : "secondary"
                    active: !editorPreview.retouchBeforeAfterEnabled
                    onClicked: editorPreview.retouchBeforeAfterEnabled = false
                }
            }
        }
    }

    MaskSection {
        objectName: "activeMaskControlsSection"
        width: parent.width
        title: editorPreview.activeMaskToolRole === "retouch" ? "Retouch Properties" : "Selection Properties"
        visible: editorPreview.hasActiveMask

        ToneTextField {
            id: activeMaskNameField
            objectName: "activeMaskRenameField"
            width: parent.width
            text: editorPreview.activeMaskName
            placeholderText: editorPreview.activeMaskToolRole === "retouch" ? "Retouch name" : "Selection set name"
            enabled: !editorPreview.activeMaskLocked
            onEditingFinished: editorPreview.activeMaskName = text
        }

        ToneTextField {
            width: parent.width
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch")
            text: editorPreview.inpaintPrompt
            placeholderText: "Optional inpaint prompt"
            enabled: !editorPreview.activeMaskCommitted
            onEditingFinished: editorPreview.inpaintPrompt = text
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch")

            ToneTextField {
                Layout.fillWidth: true
                text: editorPreview.inpaintSeed.toString()
                placeholderText: "Seed"
                enabled: !editorPreview.activeMaskCommitted
                onEditingFinished: editorPreview.inpaintSeed = Math.max(1, parseInt(text) || 1)
            }

            ActionButton {
                Layout.preferredWidth: 112
                text: "Regenerate"
                enabled: !editorPreview.activeMaskCommitted
                onClicked: editorPreview.regenerateInpaintSeed()
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch")

            ActionButton {
                Layout.fillWidth: true
                text: "Preview"
                active: editorPreview.inpaintQualityMode === "preview"
                enabled: !editorPreview.activeMaskCommitted
                onClicked: editorPreview.inpaintQualityMode = "preview"
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Final"
                active: editorPreview.inpaintQualityMode === "final"
                enabled: !editorPreview.activeMaskCommitted
                onClicked: editorPreview.inpaintQualityMode = "final"
            }
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch")
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "INPAINT BLEND"
            from: 0
            to: 100
            stepSize: 1
            defaultValue: 100
            value: Math.round(editorPreview.inpaintBlendAmount * 100)
            valueText: Math.round(editorPreview.inpaintBlendAmount * 100) + "%"
            accent: root.activeMaskAccent
            onValueDragged: function(value) { editorPreview.inpaintBlendAmount = value / 100 }
            onResetRequested: editorPreview.inpaintBlendAmount = 1.0
        }

        ActionButton {
            width: parent.width
            visible: editorPreview.activeMaskToolRole === "retouch" &&
                     (editorPreview.activeMaskRetouchMode === "remove" ||
                      editorPreview.activeMaskRetouchMode === "heal" ||
                      editorPreview.activeMaskRetouchMode === "patch") &&
                     editorPreview.inpaintPatchDiagnosticsAvailable &&
                     editorPreview.activeInpaintPatchBounds.width > 0
            text: editorPreview.inpaintPatchDiagnosticsEnabled ? "Hide Patch Bounds" : "Show Patch Bounds"
            active: editorPreview.inpaintPatchDiagnosticsEnabled
            accent: "#f2c56b"
            onClicked: editorPreview.inpaintPatchDiagnosticsEnabled = !editorPreview.inpaintPatchDiagnosticsEnabled
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskToolRole !== "retouch" &&
                     editorPreview.activeMaskKind !== "brush"

            ActionButton {
                objectName: "activeMaskOperationAddButton"
                Layout.fillWidth: true
                text: "Add"
                tooltipText: "Add this source to the selection set."
                active: editorPreview.activeMaskOperation === "add"
                onClicked: editorPreview.activeMaskOperation = "add"
            }

            ActionButton {
                objectName: "activeMaskOperationSubtractButton"
                Layout.fillWidth: true
                text: "Subtract"
                tooltipText: "Remove this source from the selection set."
                active: editorPreview.activeMaskOperation === "subtract"
                onClicked: editorPreview.activeMaskOperation = "subtract"
            }

            ActionButton {
                objectName: "activeMaskOperationIntersectButton"
                Layout.fillWidth: true
                text: "Intersect"
                tooltipText: "Keep only the overlap with the selection set."
                active: editorPreview.activeMaskOperation === "intersect"
                onClicked: editorPreview.activeMaskOperation = "intersect"
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskToolRole !== "retouch" &&
                     editorPreview.activeMaskKind !== "brush"

            ActionButton {
                objectName: "activeMaskOperationExcludeButton"
                Layout.fillWidth: true
                text: "Exclude"
                tooltipText: "Exclude overlapping pixels from this source."
                active: editorPreview.activeMaskOperation === "exclude"
                onClicked: editorPreview.activeMaskOperation = "exclude"
            }

            ActionButton {
                objectName: "activeMaskOperationReplaceButton"
                Layout.fillWidth: true
                text: "Replace"
                tooltipText: "Use this source as the selection set result."
                active: editorPreview.activeMaskOperation === "replace"
                onClicked: editorPreview.activeMaskOperation = "replace"
            }

            ActionButton {
                objectName: "activeMaskOperationUnionButton"
                Layout.fillWidth: true
                text: "Union"
                tooltipText: "Union this source with the current selection set."
                active: editorPreview.activeMaskOperation === "union"
                onClicked: editorPreview.activeMaskOperation = "union"
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                objectName: "activeMaskHideButton"
                Layout.fillWidth: true
                text: editorPreview.activeMaskEnabled ? "Hide" : "Show"
                active: editorPreview.activeMaskEnabled
                enabled: !editorPreview.activeMaskLocked
                onClicked: editorPreview.activeMaskEnabled = !editorPreview.activeMaskEnabled
            }

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskRetouchMode.length > 0
                      ? editorPreview.activeMaskRetouchMode + " retouch"
                      : (editorPreview.activeMaskKind.length > 0 ? editorPreview.activeMaskKind + " source" : "")
                horizontalAlignment: Text.AlignRight
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 8
                elide: Text.ElideNone
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                objectName: "activeMaskSoloButton"
                Layout.fillWidth: true
                text: editorPreview.activeMaskSolo ? "Soloed" : "Solo"
                active: editorPreview.activeMaskSolo
                enabled: !editorPreview.activeMaskLocked
                onClicked: editorPreview.activeMaskSolo = !editorPreview.activeMaskSolo
            }

            ActionButton {
                objectName: "activeMaskLockButton"
                Layout.fillWidth: true
                text: editorPreview.activeMaskLocked ? "Unlock" : "Lock"
                active: editorPreview.activeMaskLocked
                accent: editorPreview.activeMaskLocked ? "#d98962" : root.activeMaskAccent
                onClicked: editorPreview.activeMaskLocked = !editorPreview.activeMaskLocked
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                objectName: "activeMaskInvertButton"
                Layout.fillWidth: true
                text: editorPreview.activeMaskInverted ? "Inverted" : "Normal"
                active: editorPreview.activeMaskInverted
                tooltipText: editorPreview.activeMaskInverted ? "Return this selection source to normal." : "Invert this selection source."
                onClicked: editorPreview.activeMaskInverted = !editorPreview.activeMaskInverted
            }

            ToneComboBox {
                id: maskBlendModeCombo
                objectName: "activeMaskBlendModeCombo"
                Layout.fillWidth: true
                accent: root.activeMaskAccent
                model: ["normal", "multiply", "screen", "overlay", "lighten", "darken", "replace"]
                currentIndex: Math.max(0, model.indexOf(editorPreview.activeMaskBlendMode))
                enabled: editorPreview.hasActiveMask
                onActivated: editorPreview.activeMaskBlendMode = model[index]
            }
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            readonly property string terminologyContract: root.maskLayerParameterTerminologyContract
            readonly property string parameterRole: "mask-opacity"
            label: "MASK OPACITY"
            from: 0
            to: 100
            stepSize: 1
            defaultValue: 100
            value: Math.round(editorPreview.activeMaskOpacity * 100)
            valueText: Math.round(editorPreview.activeMaskOpacity * 100) + "%"
            accent: root.activeMaskAccent
            onValueDragged: function(value) { editorPreview.activeMaskOpacity = value / 100 }
            onResetRequested: editorPreview.activeMaskOpacity = 1.0
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            readonly property string terminologyContract: root.maskLayerParameterTerminologyContract
            readonly property string parameterRole: "mask-density"
            label: "MASK DENSITY"
            from: 0
            to: 100
            stepSize: 1
            defaultValue: 100
            value: Math.round(editorPreview.activeMaskDensity * 100)
            valueText: Math.round(editorPreview.activeMaskDensity * 100) + "%"
            accent: root.activeMaskAccent
            onValueDragged: function(value) { editorPreview.activeMaskDensity = value / 100 }
            onResetRequested: editorPreview.activeMaskDensity = 1.0
        }

        Label {
            width: parent.width
            visible: editorPreview.activeMaskToolRole !== "retouch"
            text: "Selections inside the same selection set share the same adjustments. Pick any source in the set to grade the whole selection."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MaskSection {
        width: parent.width
        title: "Looks"
        subtitle: "Fast local adjustments before the full control stack."
        visible: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        expandable: true
        expanded: false

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Warm"
                accent: "#d98962"
                active: editRoot.activeGradePreset === "warm"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("warm")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Cool"
                accent: "#7cb5ff"
                active: editRoot.activeGradePreset === "cool"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("cool")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Pop"
                accent: "#c99a52"
                active: editRoot.activeGradePreset === "pop"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("pop")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Desat"
                accent: "#d8d2ca"
                active: editRoot.activeGradePreset === "desat"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("desat")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Warm Tint"
                accent: "#d98962"
                active: editRoot.activeGradePreset === "tint-warm"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("tint-warm")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Cool Tint"
                accent: "#5e95d3"
                active: editRoot.activeGradePreset === "tint-cool"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("tint-cool")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Lift"
                accent: "#f0c987"
                active: editRoot.activeGradePreset === "lift"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("lift")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Burn"
                accent: "#8f6751"
                active: editRoot.activeGradePreset === "burn"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("burn")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Soften"
                accent: "#bca89d"
                active: editRoot.activeGradePreset === "soften"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("soften")
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Teal Pop"
                accent: "#4fa6aa"
                active: editRoot.activeGradePreset === "teal-pop"
                tone: active ? "primary" : "secondary"
                onClicked: editRoot.applyActiveMaskGradePreset("teal-pop")
            }

            ActionButton {
                Layout.fillWidth: false
                text: "Reset"
                iconName: "reset"
                showText: false
                tooltipText: "Reset mask grade"
                Accessible.name: "Reset mask grade"
                onClicked: editRoot.applyActiveMaskGradePreset("reset")
            }
        }
    }

    MaskSection {
        objectName: "maskSelectionSection"
        width: parent.width
        title: "Selection"
        subtitle: "Only the essentials. Use Edge below when you need more control."
        visible: editRoot.activeMaskSupportsSelectionRefine() &&
                 editorPreview.activeMaskToolRole !== "retouch"

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Invert"
                active: editorPreview.activeMaskSelectionInverted
                onClicked: editorPreview.activeMaskSelectionInverted = !editorPreview.activeMaskSelectionInverted
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Reset Edge"
                tooltip: "Reset matte refinement while preserving the source geometry and local adjustments."
                onClicked: editorPreview.resetActiveMaskRefinement()
            }

            ActionButton {
                objectName: "maskInspectorShowOriginalRefinementButton"
                Layout.fillWidth: true
                text: "Show Original"
                active: editorPreview.maskRefinementPreviewMode === "original"
                tooltipText: "Preview this selection before edge refinement without changing the recipe."
                onClicked: editorPreview.maskRefinementPreviewMode = "original"
            }

            ActionButton {
                objectName: "maskInspectorShowRefinedRefinementButton"
                Layout.fillWidth: true
                text: "Show Refined"
                active: editorPreview.maskRefinementPreviewMode !== "original"
                tooltipText: "Preview this selection with current edge refinement."
                onClicked: editorPreview.maskRefinementPreviewMode = "refined"
            }

            ActionButton {
                objectName: "maskInspectorHighQualityRefinementPreviewButton"
                Layout.fillWidth: true
                text: "High Quality"
                active: editorPreview.maskRefinementHighQualityPreview
                tooltipText: "Use a higher-resolution selection refinement preview for expensive edge checks."
                onClicked: editorPreview.maskRefinementHighQualityPreview = !editorPreview.maskRefinementHighQualityPreview
            }

            ActionButton {
                objectName: "maskInspectorFullResolutionPreviewButton"
                Layout.fillWidth: true
                text: "Full Res"
                active: editorPreview.maskFullResolutionPreview
                tooltipText: "Use full-resolution mask alpha for the active preview without scheduling a full image render."
                onClicked: editorPreview.maskFullResolutionPreview = !editorPreview.maskFullResolutionPreview
            }

        }

        Label {
            width: parent.width
            text: editorPreview.activeMaskSelectionInverted
                  ? "The selection is inverted. Adjustments now target everything outside the visible boundary."
                  : "Brush Add and Brush Erase paint the selection directly. Use Edge below only when the painted selection needs cleanup."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MaskSection {
        objectName: "maskEdgeSection"
        width: parent.width
        title: root.activeRefineEffectCount() > 0
               ? "Edge · " + root.activeRefineEffectCount().toString() + " active"
               : "Edge"
        subtitle: root.activeRefineEffectCount() > 0
                  ? root.activeRefineEffectCount().toString() + " advanced refine changes active."
                  : "Advanced refinement is collapsed until needed."
        visible: editRoot.activeMaskSupportsSelectionRefine() &&
                 editorPreview.activeMaskToolRole !== "retouch"
        expandable: true
        expanded: false

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: true
                text: "Refine Add"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    if (editRoot.isAiMaskKind(editorPreview.activeMaskKind))
                        editorPreview.startAIMaskRefineBrush(false)
                    else
                        editorPreview.startSelectionEdgeBrush(false)
                }
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Refine Erase"
                compact: true
                onClicked: {
                    editRoot.clearTransientTools()
                    if (editRoot.isAiMaskKind(editorPreview.activeMaskKind))
                        editorPreview.startAIMaskRefineBrush(true)
                    else
                        editorPreview.startSelectionEdgeBrush(true)
                }
            }
        }

        Label {
            width: parent.width
            text: "Brush Refine · Shift Edge · Contrast Edge · Decontaminate"
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }

        Rectangle {
            id: maskRefineEffectMeter
            objectName: "maskRefineEffectMeter"
            property real expandStrength: Math.min(1, Math.abs(editorPreview.activeMaskSelectionExpand) / 100)
            property real featherStrength: Math.min(1, Math.abs(editorPreview.activeMaskFeather - editRoot.activeMaskResetFeather()) / 0.95)
            property real smoothStrength: Math.min(1, editorPreview.activeMaskSelectionSmooth / 100)
            property real fineEdgeStrength: Math.min(1, editorPreview.activeMaskSelectionFineEdge / 100)
            property real edgeContrastStrength: Math.min(1, editorPreview.activeMaskSelectionEdgeContrast / 100)
            property real decontaminateStrength: Math.min(1, editorPreview.activeMaskSelectionDecontaminate / 100)
            property int activeEffectCount: root.activeRefineEffectCount()
            property real shiftEdgePreviewOffset: Math.max(-34, Math.min(34, editorPreview.activeMaskSelectionExpand / 100 * 34))
            property real contrastEdgePreviewWidth: Math.max(5, 34 - edgeContrastStrength * 24)

            width: parent.width
            radius: 8
            color: "#11161a"
            border.width: 1
            border.color: maskRefineEffectMeter.activeEffectCount > 0 ? Qt.alpha(root.activeMaskAccent, 0.46) : "#252c32"
            implicitHeight: 114

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: maskRefineEffectMeter.activeEffectCount > 0 ? maskRefineEffectMeter.activeEffectCount + " refine effects active" : "Neutral refine"
                        color: maskRefineEffectMeter.activeEffectCount > 0 ? "#f2f4f6" : "#8f98a2"
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: editorPreview.activeMaskSelectionInverted ? "Inverted" : "Normal"
                        color: editorPreview.activeMaskSelectionInverted ? "#d98962" : "#8f98a2"
                        font.family: theme.fontUi
                        font.pixelSize: 10
                    }
                }

                Rectangle {
                    objectName: "maskEdgeRefinePreview"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    radius: 5
                    color: "#0a0d10"
                    border.width: 1
                    border.color: "#20272d"
                    clip: true

                    Rectangle {
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 1
                        color: "#47515a"
                    }

                    Rectangle {
                        id: maskEdgePreviewContrastBand
                        objectName: "maskEdgePreviewContrastBand"
                        property real previewWidth: maskRefineEffectMeter.contrastEdgePreviewWidth
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: maskEdgePreviewShiftMarker.horizontalCenter
                        width: previewWidth
                        height: parent.height - 8
                        radius: 4
                        color: Qt.alpha("#f2c56b", 0.24 + maskRefineEffectMeter.edgeContrastStrength * 0.34)
                        border.width: 1
                        border.color: Qt.alpha("#f2c56b", 0.36 + maskRefineEffectMeter.edgeContrastStrength * 0.40)
                    }

                    Rectangle {
                        id: maskEdgePreviewShiftMarker
                        objectName: "maskEdgePreviewShiftMarker"
                        property real previewOffset: maskRefineEffectMeter.shiftEdgePreviewOffset
                        x: Math.round((parent.width - width) / 2 + previewOffset)
                        y: 3
                        width: 3
                        height: parent.height - 6
                        radius: 1
                        color: editorPreview.activeMaskSelectionExpand < 0 ? "#d98962" : root.activeMaskAccent
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    Repeater {
                        model: root.refineStrengths()

                        delegate: ColumnLayout {
                            required property var modelData

                            Layout.fillWidth: true
                            spacing: 3

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 8
                                radius: 4
                                color: "#0a0d10"
                                border.width: 1
                                border.color: "#20272d"
                                clip: true

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: Math.max(2, parent.width * modelData.value)
                                    radius: 4
                                    color: Qt.alpha(modelData.accent, modelData.value > 0.01 ? 0.88 : 0.22)
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.label
                                horizontalAlignment: Text.AlignHCenter
                                color: modelData.value > 0.01 ? "#cfd5db" : "#65707a"
                                font.family: theme.fontUi
                                font.pixelSize: 9
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 7
                                elide: Text.ElideNone
                            }
                        }
                    }
                }
            }
        }

        InspectorSlider {
            objectName: "activeMaskShiftEdgeSlider"
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Shift Edge"
            valueText: Math.round(editorPreview.activeMaskSelectionExpand).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSelectionExpand
            onValueDragged: function(value) { editorPreview.activeMaskSelectionExpand = value }
            onResetRequested: editorPreview.activeMaskSelectionExpand = 0
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Feather"
            valueText: editorPreview.activeMaskFeather.toFixed(2)
            from: (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.0 : 0.05
            to: (editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso") ? 0.2 : 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskFeather
            onValueDragged: function(value) { editorPreview.activeMaskFeather = value }
            onResetRequested: editorPreview.activeMaskFeather = editRoot.activeMaskResetFeather()
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Smooth"
            valueText: Math.round(editorPreview.activeMaskSelectionSmooth).toString()
            from: 0
            to: 100
            value: editorPreview.activeMaskSelectionSmooth
            onValueDragged: function(value) { editorPreview.activeMaskSelectionSmooth = value }
            onResetRequested: editorPreview.activeMaskSelectionSmooth = 0
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Fine Edge"
            valueText: Math.round(editorPreview.activeMaskSelectionFineEdge).toString()
            from: 0
            to: 100
            value: editorPreview.activeMaskSelectionFineEdge
            onValueDragged: function(value) { editorPreview.activeMaskSelectionFineEdge = value }
            onResetRequested: editorPreview.activeMaskSelectionFineEdge = 0
        }

        InspectorSlider {
            objectName: "activeMaskContrastEdgeSlider"
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Contrast Edge"
            valueText: Math.round(editorPreview.activeMaskSelectionEdgeContrast).toString()
            from: 0
            to: 100
            value: editorPreview.activeMaskSelectionEdgeContrast
            onValueDragged: function(value) { editorPreview.activeMaskSelectionEdgeContrast = value }
            onResetRequested: editorPreview.activeMaskSelectionEdgeContrast = 0
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Decontaminate"
            valueText: Math.round(editorPreview.activeMaskSelectionDecontaminate).toString()
            from: 0
            to: 100
            value: editorPreview.activeMaskSelectionDecontaminate
            onValueDragged: function(value) { editorPreview.activeMaskSelectionDecontaminate = value }
            onResetRequested: editorPreview.activeMaskSelectionDecontaminate = 0
        }
    }

    MaskSection {
        objectName: "activeMaskAdjustmentSection"
        width: parent.width
        title: "Adjust"
        subtitle: editorPreview.activeMaskToolRole === "retouch"
                  ? "Retouch uses its own stroke settings. Image adjustments apply only to selection sets."
                  : (editRoot.activeMaskHasAdjustments()
                     ? "The current selection set is modifying tone or color."
                     : "Collapsed until the current selection set needs image adjustments.")
        expandable: true
        expanded: editRoot.activeMaskHasAdjustments()
        visible: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"

        RowLayout {
            objectName: "maskAdjustToneGroup"
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: "Tone"
                color: "#aeb7c1"
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            ActionButton {
                objectName: "maskAdjustToneBeforeAfterButton"
                compact: true
                showText: false
                iconName: root.maskCompareGroupActive("tone") ? "hidden" : "visible"
                active: root.maskCompareGroupActive("tone")
                enabled: editorPreview.hasActiveMask
                tooltipText: root.maskCompareGroupActive("tone")
                             ? "Restore local Tone in before/after."
                             : "Compare before and after for local Tone without changing values."
                onClicked: root.toggleMaskCompareGroup("tone")
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Exposure"
            valueText: editorPreview.activeMaskExposure.toFixed(2) + " EV"
            from: -2
            to: 2
            value: editorPreview.activeMaskExposure
            onValueDragged: function(value) { editorPreview.activeMaskExposure = value }
            onResetRequested: editorPreview.activeMaskExposure = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Contrast"
            valueText: Math.round(editorPreview.activeMaskContrast).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskContrast
            onValueDragged: function(value) { editorPreview.activeMaskContrast = value }
            onResetRequested: editorPreview.activeMaskContrast = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Highlights"
            valueText: Math.round(editorPreview.activeMaskHighlights).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskHighlights
            onValueDragged: function(value) { editorPreview.activeMaskHighlights = value }
            onResetRequested: editorPreview.activeMaskHighlights = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Shadows"
            valueText: Math.round(editorPreview.activeMaskShadows).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskShadows
            onValueDragged: function(value) { editorPreview.activeMaskShadows = value }
            onResetRequested: editorPreview.activeMaskShadows = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Whites"
            valueText: Math.round(editorPreview.activeMaskWhites).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskWhites
            onValueDragged: function(value) { editorPreview.activeMaskWhites = value }
            onResetRequested: editorPreview.activeMaskWhites = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Blacks"
            valueText: Math.round(editorPreview.activeMaskBlacks).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskBlacks
            onValueDragged: function(value) { editorPreview.activeMaskBlacks = value }
            onResetRequested: editorPreview.activeMaskBlacks = 0
        }

        RowLayout {
            objectName: "maskAdjustColorGroup"
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: "Color"
                color: "#aeb7c1"
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            ActionButton {
                objectName: "maskAdjustColorBeforeAfterButton"
                compact: true
                showText: false
                iconName: root.maskCompareGroupActive("color") ? "hidden" : "visible"
                active: root.maskCompareGroupActive("color")
                enabled: editorPreview.hasActiveMask
                tooltipText: root.maskCompareGroupActive("color")
                             ? "Restore local Color in before/after."
                             : "Compare before and after for local Color without changing values."
                onClicked: root.toggleMaskCompareGroup("color")
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Temperature"
            valueText: Math.round(editorPreview.activeMaskTemperature).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskTemperature
            onValueDragged: function(value) { editorPreview.activeMaskTemperature = value }
            onResetRequested: editorPreview.activeMaskTemperature = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Tint"
            valueText: Math.round(editorPreview.activeMaskTint).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskTint
            onValueDragged: function(value) { editorPreview.activeMaskTint = value }
            onResetRequested: editorPreview.activeMaskTint = 0
        }

        RowLayout {
            objectName: "maskAdjustPresenceGroup"
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: "Presence"
                color: "#aeb7c1"
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            ActionButton {
                objectName: "maskAdjustPresenceBeforeAfterButton"
                compact: true
                showText: false
                iconName: root.maskCompareGroupActive("presence") ? "hidden" : "visible"
                active: root.maskCompareGroupActive("presence")
                enabled: editorPreview.hasActiveMask
                tooltipText: root.maskCompareGroupActive("presence")
                             ? "Restore local Presence in before/after."
                             : "Compare before and after for local Presence without changing values."
                onClicked: root.toggleMaskCompareGroup("presence")
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Vibrance"
            valueText: Math.round(editorPreview.activeMaskVibrance).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskVibrance
            onValueDragged: function(value) { editorPreview.activeMaskVibrance = value }
            onResetRequested: editorPreview.activeMaskVibrance = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Overall Saturation"
            valueText: Math.round(editorPreview.activeMaskSaturation).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSaturation
            onValueDragged: function(value) { editorPreview.activeMaskSaturation = value }
            onResetRequested: editorPreview.activeMaskSaturation = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Shift"
            valueText: Math.round(editorPreview.activeMaskHueShift * 1.8).toString() + "°"
            from: -100
            to: 100
            value: editorPreview.activeMaskHueShift
            onValueDragged: function(value) { editorPreview.activeMaskHueShift = value }
            onResetRequested: editorPreview.activeMaskHueShift = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Chroma Shift"
            valueText: Math.round(editorPreview.activeMaskSaturationShift).toString()
            from: -100
            to: 100
            value: editorPreview.activeMaskSaturationShift
            onValueDragged: function(value) { editorPreview.activeMaskSaturationShift = value }
            onResetRequested: editorPreview.activeMaskSaturationShift = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Tint Hue"
            valueText: Math.round(editorPreview.activeMaskColorizeHue * 360).toString() + "°"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskColorizeHue
            onValueDragged: function(value) { editorPreview.activeMaskColorizeHue = value }
            onResetRequested: editorPreview.activeMaskColorizeHue = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Tint Amount"
            valueText: Math.round(editorPreview.activeMaskColorizeStrength).toString() + "%"
            from: 0
            to: 100
            value: editorPreview.activeMaskColorizeStrength
            onValueDragged: function(value) { editorPreview.activeMaskColorizeStrength = value }
            onResetRequested: editorPreview.activeMaskColorizeStrength = 0
        }

        RowLayout {
            objectName: "maskAdjustEffectsGroup"
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: "Effects"
                color: "#aeb7c1"
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            ActionButton {
                objectName: "maskAdjustEffectsBeforeAfterButton"
                compact: true
                showText: false
                iconName: root.maskCompareGroupActive("effects") ? "hidden" : "visible"
                active: root.maskCompareGroupActive("effects")
                enabled: editorPreview.hasActiveMask
                tooltipText: root.maskCompareGroupActive("effects")
                             ? "Restore local Effects in before/after."
                             : "Compare before and after for local Effects without changing values."
                onClicked: root.toggleMaskCompareGroup("effects")
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Remove"
            valueText: Math.round(editorPreview.activeMaskRemoveStrength).toString() + "%"
            from: 0
            to: 100
            value: editorPreview.activeMaskRemoveStrength
            onValueDragged: function(value) { editorPreview.activeMaskRemoveStrength = value }
            onResetRequested: editorPreview.activeMaskRemoveStrength = 0
        }
    }

    MaskSection {
        objectName: "maskShapeSection"
        width: parent.width
        title: "Shape"
        visible: editorPreview.hasActiveMask &&
                 editorPreview.activeMaskToolRole !== "retouch" &&
                 (editorPreview.activeMaskKind === "linear" || editorPreview.activeMaskKind === "radial")
        expandable: true
        expanded: true

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "linear"
            label: "Position"
            valueText: editorPreview.activeMaskPosition.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskPosition
            onValueDragged: function(value) { editorPreview.activeMaskPosition = value }
            onResetRequested: editorPreview.activeMaskPosition = 0.5
        }

        InspectorSlider {
            width: parent.width
            label: "Rotation"
            valueText: Math.round(editorPreview.activeMaskRotation).toString() + "°"
            from: -180
            to: 180
            stepSize: 1
            value: editorPreview.activeMaskRotation
            onValueDragged: function(value) { editorPreview.activeMaskRotation = value }
            onResetRequested: editorPreview.activeMaskRotation = 0
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Center X"
            valueText: editorPreview.activeMaskCenterX.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskCenterX
            onValueDragged: function(value) { editorPreview.activeMaskCenterX = value }
            onResetRequested: editorPreview.activeMaskCenterX = 0.5
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Center Y"
            valueText: editorPreview.activeMaskCenterY.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskCenterY
            onValueDragged: function(value) { editorPreview.activeMaskCenterY = value }
            onResetRequested: editorPreview.activeMaskCenterY = 0.5
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Radius X"
            valueText: editorPreview.activeMaskRadiusX.toFixed(2)
            from: 0.05
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRadiusX
            onValueDragged: function(value) { editorPreview.activeMaskRadiusX = value }
            onResetRequested: editorPreview.activeMaskRadiusX = 0.25
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskKind === "radial"
            label: "Radius Y"
            valueText: editorPreview.activeMaskRadiusY.toFixed(2)
            from: 0.05
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRadiusY
            onValueDragged: function(value) { editorPreview.activeMaskRadiusY = value }
            onResetRequested: editorPreview.activeMaskRadiusY = 0.25
        }
    }

    MaskSection {
        width: parent.width
        title: "Brush"
        visible: editorPreview.activeMaskKind === "brush"
        expandable: true
        expanded: true

        Label {
            width: parent.width
            visible: editorPreview.activeMaskRetouchMode === "remove" ||
                     editorPreview.activeMaskRetouchMode === "heal" ||
                     editorPreview.activeMaskRetouchMode === "patch" ||
                     editorPreview.activeMaskRetouchMode === "clone"
            text: editorPreview.activeMaskRetouchMode === "remove"
                  ? "Paint over unwanted content to remove it from the image."
                  : editorPreview.activeMaskRetouchMode === "heal"
                    ? "Heal uses a softer fill pass for blemishes and seams."
                    : editorPreview.activeMaskRetouchMode === "patch"
                      ? "Patch fills a painted repair area with an auto-masked retouch pass."
                      : (editorPreview.activeMaskCloneSourceSet
                         ? "Clone Stamp is ready. Paint to stamp from the marked source point."
                         : "Clone Stamp needs a source point before you paint.")
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskRetouchMode === "clone"

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskCloneSourceSet ? "Repick Source" : "Pick Source"
                compact: true
                active: editRoot.cloneSourcePickArmed
                onClicked: editRoot.selectTool(editRoot.cloneSourcePickArmed ? "" : "clone-source")
            }

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskCloneSourceSet
                      ? ("Source: " + Math.round(editorPreview.activeMaskCloneSourceX * 100).toString() + ", " +
                         Math.round(editorPreview.activeMaskCloneSourceY * 100).toString())
                      : "Source not set"
                horizontalAlignment: Text.AlignRight
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
            }
        }

        RowLayout {
            objectName: "maskAdjustDetailGroup"
            width: parent.width
            visible: editorPreview.activeMaskRetouchMode === "heal"
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: "Detail"
                color: "#aeb7c1"
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            ActionButton {
                objectName: "maskAdjustDetailBeforeAfterButton"
                compact: true
                showText: false
                iconName: root.maskCompareGroupActive("detail") ? "hidden" : "visible"
                active: root.maskCompareGroupActive("detail")
                enabled: editorPreview.hasActiveMask
                tooltipText: root.maskCompareGroupActive("detail")
                             ? "Restore local Detail in before/after."
                             : "Compare before and after for local Detail without changing values."
                onClicked: root.toggleMaskCompareGroup("detail")
            }
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskRetouchMode === "heal"
            label: "Heal Texture"
            valueText: Math.round(editorPreview.activeMaskHealTextureBlend * 100).toString() + "%"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskHealTextureBlend
            onValueDragged: function(value) { editorPreview.activeMaskHealTextureBlend = value }
            onResetRequested: editorPreview.activeMaskHealTextureBlend = 0.68
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskRetouchMode === "heal"
            label: "Heal Color"
            valueText: Math.round(editorPreview.activeMaskHealColorBlend * 100).toString() + "%"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskHealColorBlend
            onValueDragged: function(value) { editorPreview.activeMaskHealColorBlend = value }
            onResetRequested: editorPreview.activeMaskHealColorBlend = 0.46
        }

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            visible: editorPreview.activeMaskKind === "brush"

            Repeater {
                model: editorPreview.brushPresetNames

                ActionButton {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    compact: true
                    active: editorPreview.activeMaskBrushPreset === modelData
                    accent: root.activeMaskAccent
                    onClicked: editorPreview.applyActiveMaskBrushPreset(modelData)
                }
            }
        }

        GridLayout {
            width: parent.width
            columns: 3
            columnSpacing: 8
            rowSpacing: 8
            visible: editorPreview.activeMaskKind === "brush"

            ActionButton {
                Layout.fillWidth: true
                text: "Save Custom"
                compact: true
                onClicked: editorPreview.saveActiveMaskBrushPreset("Custom Brush", true)
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Delete Custom"
                compact: true
                enabled: editorPreview.isCustomBrushPreset(editorPreview.activeMaskBrushPreset)
                onClicked: editorPreview.deleteCustomBrushPreset(editorPreview.activeMaskBrushPreset)
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Reset Custom"
                compact: true
                onClicked: editorPreview.resetCustomBrushPresets()
            }
        }

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            visible: editorPreview.activeMaskRetouchMode === "clone"

            ActionButton {
                Layout.fillWidth: true
                text: editorPreview.activeMaskCloneAligned ? "Aligned" : "Non-aligned"
                compact: true
                active: editorPreview.activeMaskCloneAligned
                onClicked: editorPreview.activeMaskCloneAligned = !editorPreview.activeMaskCloneAligned
            }

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskCloneScale.toFixed(2) + "x / " +
                      Math.round(editorPreview.activeMaskCloneRotation).toString() + "°"
                horizontalAlignment: Text.AlignRight
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Rotate -15°"
                compact: true
                onClicked: editorPreview.activeMaskCloneRotation = editorPreview.activeMaskCloneRotation - 15
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Rotate +15°"
                compact: true
                onClicked: editorPreview.activeMaskCloneRotation = editorPreview.activeMaskCloneRotation + 15
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Scale Down"
                compact: true
                onClicked: editorPreview.activeMaskCloneScale = editorPreview.activeMaskCloneScale - 0.1
            }

            ActionButton {
                Layout.fillWidth: true
                text: "Scale Up"
                compact: true
                onClicked: editorPreview.activeMaskCloneScale = editorPreview.activeMaskCloneScale + 0.1
            }
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Brush Size"
            valueText: Math.round(editorPreview.activeMaskBrushSize * 100).toString()
            from: 0.01
            to: 0.35
            stepSize: 0.01
            value: editorPreview.activeMaskBrushSize
            onValueDragged: function(value) { editorPreview.activeMaskBrushSize = value }
            onResetRequested: editorPreview.activeMaskBrushSize = 0.08
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            readonly property string terminologyContract: root.maskLayerParameterTerminologyContract
            readonly property string parameterRole: "brush-flow"
            label: "Brush Flow"
            valueText: Math.round(editorPreview.activeMaskBrushFlow * 100).toString() + "%"
            from: 0.05
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushFlow
            onValueDragged: function(value) { editorPreview.activeMaskBrushFlow = value }
            onResetRequested: editorPreview.activeMaskBrushFlow = 1.0
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Brush Hardness"
            valueText: Math.round(editorPreview.activeMaskBrushHardness * 100).toString() + "%"
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushHardness
            onValueDragged: function(value) { editorPreview.activeMaskBrushHardness = value }
            onResetRequested: editorPreview.activeMaskBrushHardness = 0.25
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Brush Spacing"
            valueText: Math.round(editorPreview.activeMaskBrushSpacing * 100).toString() + "% of diameter"
            from: 0.05
            to: 2.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushSpacing
            onValueDragged: function(value) { editorPreview.activeMaskBrushSpacing = value }
            onResetRequested: editorPreview.activeMaskBrushSpacing = 0.75
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Stroke Smoothing"
            valueText: Math.round(editorPreview.activeMaskBrushSmoothing * 100).toString() + "%"
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushSmoothing
            onValueDragged: function(value) { editorPreview.activeMaskBrushSmoothing = value }
            onResetRequested: editorPreview.activeMaskBrushSmoothing = 0.0
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Pressure Size"
            valueText: Math.round(editorPreview.activeMaskBrushPressureSize * 100).toString() + "%"
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushPressureSize
            onValueDragged: function(value) { editorPreview.activeMaskBrushPressureSize = value }
            onResetRequested: editorPreview.activeMaskBrushPressureSize = 0.65
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            label: "Pressure Flow"
            valueText: Math.round(editorPreview.activeMaskBrushPressureFlow * 100).toString() + "%"
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushPressureFlow
            onValueDragged: function(value) { editorPreview.activeMaskBrushPressureFlow = value }
            onResetRequested: editorPreview.activeMaskBrushPressureFlow = 1.0
        }

        InspectorSlider {
            width: parent.width
            readonly property string targetSupportContract: root.adjustmentControlTargetSupportContract
            readonly property string targetSupport: "mask"
            readonly property string targetSupportSignature: root.maskControlTargetSupportSignature(targetSupport)
            readonly property string terminologyContract: root.maskLayerParameterTerminologyContract
            readonly property string parameterRole: "brush-opacity"
            label: "Pressure Opacity"
            valueText: Math.round(editorPreview.activeMaskBrushPressureOpacity * 100).toString() + "%"
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskBrushPressureOpacity
            onValueDragged: function(value) { editorPreview.activeMaskBrushPressureOpacity = value }
            onResetRequested: editorPreview.activeMaskBrushPressureOpacity = 0.0
        }

        ActionButton {
            width: parent.width
            text: editorPreview.activeMaskBrushBuildup ? "Flow Buildup" : "Single Pass Flow"
            active: editorPreview.activeMaskBrushBuildup
            compact: true
            onClicked: editorPreview.activeMaskBrushBuildup = !editorPreview.activeMaskBrushBuildup
        }

        ActionButton {
            width: parent.width
            text: editorPreview.activeMaskBrushEraseMode ? "Erase Mode" : "Paint Mode"
            active: editorPreview.activeMaskBrushEraseMode
            accent: editorPreview.activeMaskBrushEraseMode ? "#d98962" : root.activeMaskAccent
            compact: true
            onClicked: editorPreview.activeMaskBrushEraseMode = !editorPreview.activeMaskBrushEraseMode
        }

        ActionButton {
            width: parent.width
                            text: editorPreview.activeMaskBrushAutoMask ? "Edge Aware" : "Freehand"
            active: editorPreview.activeMaskBrushAutoMask
            compact: true
            onClicked: editorPreview.activeMaskBrushAutoMask = !editorPreview.activeMaskBrushAutoMask
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskBrushAutoMask
            label: "Edge-Aware Range"
            valueText: Math.round(editorPreview.activeMaskBrushAutoMaskTolerance * 100).toString() + "%"
            from: 0.02
            to: 0.75
            stepSize: 0.01
            value: editorPreview.activeMaskBrushAutoMaskTolerance
            onValueDragged: function(value) { editorPreview.activeMaskBrushAutoMaskTolerance = value }
            onResetRequested: editorPreview.activeMaskBrushAutoMaskTolerance = 0.18
        }

        ActionButton {
            width: parent.width
            visible: editorPreview.activeMaskBrushAutoMask
            text: editorPreview.activeMaskBrushAutoMaskContinuousSampling ? "Sample Each Dab" : "Sample First Dab"
            active: editorPreview.activeMaskBrushAutoMaskContinuousSampling
            compact: true
            tooltipText: editorPreview.activeMaskBrushAutoMaskContinuousSampling
                         ? "Update the protected color from every dab center during the stroke."
                         : "Lock the protected color to the first dab in the stroke."
            onClicked: editorPreview.activeMaskBrushAutoMaskContinuousSampling =
                       !editorPreview.activeMaskBrushAutoMaskContinuousSampling
        }

        ActionButton {
            width: parent.width
            visible: editorPreview.activeMaskBrushAutoMask
            text: "Protect Edges"
            active: editorPreview.activeMaskBrushProtectEdges
            compact: true
            tooltipText: "Tighten edge gating for hair, sky, and building boundaries."
            onClicked: editorPreview.activeMaskBrushProtectEdges =
                       !editorPreview.activeMaskBrushProtectEdges
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskRetouchMode === "clone"
            label: "Source Scale"
            valueText: editorPreview.activeMaskCloneScale.toFixed(2) + "x"
            from: 0.25
            to: 4.0
            stepSize: 0.01
            value: editorPreview.activeMaskCloneScale
            onValueDragged: function(value) { editorPreview.activeMaskCloneScale = value }
            onResetRequested: editorPreview.activeMaskCloneScale = 1.0
        }

        InspectorSlider {
            width: parent.width
            visible: editorPreview.activeMaskRetouchMode === "clone"
            label: "Source Rotation"
            valueText: Math.round(editorPreview.activeMaskCloneRotation).toString() + "°"
            from: -180
            to: 180
            stepSize: 1
            value: editorPreview.activeMaskCloneRotation
            onValueDragged: function(value) { editorPreview.activeMaskCloneRotation = value }
            onResetRequested: editorPreview.activeMaskCloneRotation = 0
        }

        RowLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskBrushPointCount.toString() + " brush points"
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
            }

            ActionButton {
                text: "Clear"
                compact: true
                onClicked: editorPreview.clearActiveBrushPoints()
            }
        }
    }

    MaskSection {
        width: parent.width
        title: editorPreview.activeMaskKind === "lasso" ? "Lasso" : "Polygon"
        visible: editorPreview.activeMaskKind === "polygon" || editorPreview.activeMaskKind === "lasso"
        expandable: true
        expanded: true

        RowLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: editorPreview.activeMaskPolygonPointCount.toString() + " points"
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
            }

            ActionButton {
                text: "Undo Point"
                compact: true
                onClicked: editorPreview.removeLastActivePolygonPoint()
            }

            ActionButton {
                text: "Clear"
                compact: true
                onClicked: editorPreview.clearActivePolygonPoints()
            }
        }

        RowLayout {
            width: parent.width
            spacing: 8
            visible: editorPreview.activeMaskKind === "polygon"

            ActionButton {
                text: "Simplify"
                compact: true
                enabled: editorPreview.activeMaskPolygonPointCount > 3
                onClicked: editorPreview.simplifyActivePolygon()
            }

            ActionButton {
                text: "Smooth"
                compact: true
                enabled: editorPreview.activeMaskPolygonPointCount >= 3
                onClicked: editorPreview.smoothActivePolygon()
            }
        }

        Label {
            objectName: "polygonSelfIntersectionWarning"
            width: parent.width
            visible: editorPreview.activeMaskKind === "polygon" && editorPreview.activeMaskPolygonSelfIntersecting
            text: "Self-intersecting polygon. Undo the last point or redraw the shape."
            wrapMode: Text.WordWrap
            color: theme.warning
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MaskSection {
        width: parent.width
        title: "Generated Refine"
        visible: editRoot.isAiMaskKind(editorPreview.activeMaskKind)
        expandable: true
        expanded: true

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskFeather.toFixed(2)
            from: 0.0
            to: 1.0
            stepSize: 0.01
            value: editorPreview.activeMaskFeather
            onValueDragged: function(value) { editorPreview.activeMaskFeather = value }
            onResetRequested: editorPreview.activeMaskFeather = 0.5
        }

        Label {
            width: parent.width
            text: editorPreview.activeMaskKind === "object"
                  ? "Object picks use " + root.smartSelectionRuntimeLabel() + " on the canvas. If the target cannot be isolated, refine manually."
                  : editRoot.aiMaskLabel(editorPreview.activeMaskKind) + " selections stay editable inside the current selection set."
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }

        Label {
            objectName: "generatedMaskQualitySummary"
            width: parent.width
            visible: editorPreview.activeGeneratedMaskQualitySummary.length > 0
            text: editorPreview.activeGeneratedMaskQualitySummary
            wrapMode: Text.WordWrap
            color: editorPreview.activeGeneratedMaskQualityScore < 70 ? theme.warning : "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }

        Label {
            objectName: "generatedMaskCleanupSummary"
            width: parent.width
            visible: editorPreview.activeGeneratedMaskCleanupSummary.length > 0
            text: editorPreview.activeGeneratedMaskCleanupSummary
            wrapMode: Text.WordWrap
            color: "#8f98a2"
            font.family: theme.fontUi
            font.pixelSize: 11
        }

        InspectorSlider {
            objectName: "generatedMaskHoleFillThresholdSlider"
            width: parent.width
            label: "Fill Holes"
            valueText: (editorPreview.activeGeneratedMaskHoleFillThreshold * 100).toFixed(2) + "%"
            from: 0
            to: 0.05
            stepSize: 0.0005
            value: editorPreview.activeGeneratedMaskHoleFillThreshold
            onValueDragged: function(value) { editorPreview.activeGeneratedMaskHoleFillThreshold = value }
            onResetRequested: editorPreview.activeGeneratedMaskHoleFillThreshold = 0.004
        }

        InspectorSlider {
            objectName: "generatedMaskIslandRemovalThresholdSlider"
            width: parent.width
            label: "Remove Specks"
            valueText: (editorPreview.activeGeneratedMaskIslandRemovalThreshold * 100).toFixed(2) + "%"
            from: 0
            to: 0.05
            stepSize: 0.0005
            value: editorPreview.activeGeneratedMaskIslandRemovalThreshold
            onValueDragged: function(value) { editorPreview.activeGeneratedMaskIslandRemovalThreshold = value }
            onResetRequested: editorPreview.activeGeneratedMaskIslandRemovalThreshold = 0.0015
        }
    }

    MaskSection {
        width: parent.width
        title: "Range Refine"
        subtitle: "Limit the active selection by tone, color, depth, or edge confidence."
        visible: editorPreview.hasActiveMask && editorPreview.activeMaskToolRole !== "retouch"
        expandable: true
        expanded: false

        RowLayout {
            width: parent.width
            spacing: 8

            ActionButton {
                Layout.fillWidth: false
                text: "Reset"
                iconName: "reset"
                showText: false
                tooltipText: "Reset range refine"
                Accessible.name: "Reset range refine"
                compact: true
                onClicked: {
                    editorPreview.activeMaskRefineLuminanceMin = 0
                    editorPreview.activeMaskRefineLuminanceMax = 1
                    editorPreview.activeMaskRefineHueCenter = 0
                    editorPreview.activeMaskRefineHueRange = 0.5
                    editorPreview.activeMaskRefineSaturationMin = 0
                    editorPreview.activeMaskRefineSaturationMax = 1
                    editorPreview.activeMaskRefineDepthMin = 0
                    editorPreview.activeMaskRefineDepthMax = 1
                    editorPreview.activeMaskRefineEdgeConfidenceMin = 0
                    editorPreview.activeMaskRefineRangeFeather = 0.05
                }
            }

            Label {
                Layout.fillWidth: true
                text: "Source-aware"
                horizontalAlignment: Text.AlignRight
                color: "#8f98a2"
                font.family: theme.fontUi
                font.pixelSize: 11
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 8
                elide: Text.ElideNone
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Luma Min"
            valueText: editorPreview.activeMaskRefineLuminanceMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineLuminanceMin
            onValueDragged: function(value) { editorPreview.activeMaskRefineLuminanceMin = value }
            onResetRequested: editorPreview.activeMaskRefineLuminanceMin = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Luma Max"
            valueText: editorPreview.activeMaskRefineLuminanceMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineLuminanceMax
            onValueDragged: function(value) { editorPreview.activeMaskRefineLuminanceMax = value }
            onResetRequested: editorPreview.activeMaskRefineLuminanceMax = 1
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Center"
            valueText: Math.round(editorPreview.activeMaskRefineHueCenter * 360).toString() + "°"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineHueCenter
            onValueDragged: function(value) { editorPreview.activeMaskRefineHueCenter = value }
            onResetRequested: editorPreview.activeMaskRefineHueCenter = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Width"
            valueText: Math.round(editorPreview.activeMaskRefineHueRange * 360).toString() + "°"
            from: 0.02
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskRefineHueRange
            onValueDragged: function(value) { editorPreview.activeMaskRefineHueRange = value }
            onResetRequested: editorPreview.activeMaskRefineHueRange = 0.5
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Min"
            valueText: editorPreview.activeMaskRefineSaturationMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineSaturationMin
            onValueDragged: function(value) { editorPreview.activeMaskRefineSaturationMin = value }
            onResetRequested: editorPreview.activeMaskRefineSaturationMin = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Max"
            valueText: editorPreview.activeMaskRefineSaturationMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineSaturationMax
            onValueDragged: function(value) { editorPreview.activeMaskRefineSaturationMax = value }
            onResetRequested: editorPreview.activeMaskRefineSaturationMax = 1
        }

        InspectorSlider {
            width: parent.width
            label: "Depth Near"
            valueText: editorPreview.activeMaskRefineDepthMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineDepthMin
            onValueDragged: function(value) { editorPreview.activeMaskRefineDepthMin = value }
            onResetRequested: editorPreview.activeMaskRefineDepthMin = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Depth Far"
            valueText: editorPreview.activeMaskRefineDepthMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineDepthMax
            onValueDragged: function(value) { editorPreview.activeMaskRefineDepthMax = value }
            onResetRequested: editorPreview.activeMaskRefineDepthMax = 1
        }

        InspectorSlider {
            width: parent.width
            label: "Edge Confidence"
            valueText: editorPreview.activeMaskRefineEdgeConfidenceMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskRefineEdgeConfidenceMin
            onValueDragged: function(value) { editorPreview.activeMaskRefineEdgeConfidenceMin = value }
            onResetRequested: editorPreview.activeMaskRefineEdgeConfidenceMin = 0
        }

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskRefineRangeFeather.toFixed(2)
            from: 0
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskRefineRangeFeather
            onValueDragged: function(value) { editorPreview.activeMaskRefineRangeFeather = value }
            onResetRequested: editorPreview.activeMaskRefineRangeFeather = 0.05
        }
    }

    MaskSection {
        width: parent.width
        title: "Luminance Range"
        visible: editorPreview.activeMaskKind === "luminance"
        expandable: true
        expanded: true

        RowLayout {
            width: parent.width
            spacing: 6

            MiniButton {
                objectName: "activeMaskLuminanceRangeSampleButton"
                Layout.fillWidth: true
                text: "Drag Sample"
                iconName: "eyedropper"
                role: "segmented"
                active: editRoot.maskLuminanceRangeSampling && editorPreview.activeMaskKind === "luminance"
                tooltip: "Drag a luminance patch on the image."
                enabled: editorPreview.hasAsset && editorPreview.activeMaskLuminanceSamples.length < 5
                onClicked: editRoot.setMaskLuminanceRangeSampler(!active)
            }

            Label {
                objectName: "activeMaskLuminanceRangeSampleCountLabel"
                text: editorPreview.activeMaskLuminanceSamples.length + "/5"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
                font.letterSpacing: 0
            }
        }

        RowLayout {
            objectName: "activeMaskLuminanceRangeSampleStrip"
            width: parent.width
            visible: editorPreview.activeMaskLuminanceSamples.length > 0
            spacing: 5

            Repeater {
                model: editorPreview.activeMaskLuminanceSamples

                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    readonly property real swatchLuma: Math.max(0, Math.min(1, Number(modelData.luminanceMean || 0)))
                    spacing: 3

                    Rectangle {
                        objectName: "activeMaskLuminanceRangeSampleSwatch" + index
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 18
                        radius: 3
                        color: Qt.rgba(parent.swatchLuma, parent.swatchLuma, parent.swatchLuma, 1)
                        border.color: theme.alpha(theme.polar, 0.28)
                        border.width: 1
                    }

                    MiniButton {
                        objectName: "activeMaskLuminanceRangeRemoveSampleButton" + index
                        square: true
                        iconName: "minus"
                        role: "icon"
                        tooltip: "Remove this luminance range sample."
                        onClicked: editorPreview.removeActiveLuminanceRangeSample(index)
                    }
                }
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Minimum"
            valueText: editorPreview.activeMaskLuminanceMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskLuminanceMin
            onValueDragged: function(value) { editorPreview.activeMaskLuminanceMin = value }
            onResetRequested: editorPreview.activeMaskLuminanceMin = 0.2
        }

        InspectorSlider {
            width: parent.width
            label: "Maximum"
            valueText: editorPreview.activeMaskLuminanceMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskLuminanceMax
            onValueDragged: function(value) { editorPreview.activeMaskLuminanceMax = value }
            onResetRequested: editorPreview.activeMaskLuminanceMax = 0.8
        }

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskRangeFeather.toFixed(2)
            from: 0
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskRangeFeather
            onValueDragged: function(value) { editorPreview.activeMaskRangeFeather = value }
            onResetRequested: editorPreview.activeMaskRangeFeather = 0.1
        }
    }

    MaskSection {
        width: parent.width
        title: "Color Range"
        visible: editorPreview.activeMaskKind === "color"
        expandable: true
        expanded: true

        RowLayout {
            width: parent.width
            spacing: 6

            MiniButton {
                objectName: "activeMaskColorRangeSampleButton"
                Layout.fillWidth: true
                text: "Sample"
                iconName: "eyedropper"
                role: "segmented"
                active: editRoot.maskColorRangeSampling && editorPreview.activeMaskKind === "color"
                tooltip: "Sample a color range swatch from the image."
                enabled: editorPreview.hasAsset && editorPreview.activeMaskColorSamples.length < 5
                onClicked: editRoot.setMaskColorRangeSampler(!active)
            }

            Label {
                objectName: "activeMaskColorRangeSampleCountLabel"
                text: editorPreview.activeMaskColorSamples.length + "/5"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
                font.letterSpacing: 0
            }
        }

        RowLayout {
            objectName: "activeMaskColorRangeSampleStrip"
            width: parent.width
            visible: editorPreview.activeMaskColorSamples.length > 0
            spacing: 5

            Repeater {
                model: editorPreview.activeMaskColorSamples

                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    spacing: 3

                    Rectangle {
                        objectName: "activeMaskColorRangeSampleSwatch" + index
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 18
                        radius: 3
                        color: String(modelData.hex || "#000000")
                        border.color: theme.alpha(theme.polar, 0.28)
                        border.width: 1
                    }

                    MiniButton {
                        objectName: "activeMaskColorRangeRemoveSampleButton" + index
                        square: true
                        iconName: "minus"
                        role: "icon"
                        tooltip: "Remove this color range sample."
                        onClicked: editorPreview.removeActiveColorRangeSample(index)
                    }
                }
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Center"
            valueText: Math.round(editorPreview.activeMaskHueCenter * 360).toString() + "°"
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskHueCenter
            onValueDragged: function(value) { editorPreview.activeMaskHueCenter = value }
            onResetRequested: editorPreview.activeMaskHueCenter = 0.08
        }

        InspectorSlider {
            width: parent.width
            label: "Hue Width"
            valueText: Math.round(editorPreview.activeMaskHueRange * 360).toString() + "°"
            from: 0.02
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskHueRange
            onValueDragged: function(value) { editorPreview.activeMaskHueRange = value }
            onResetRequested: editorPreview.activeMaskHueRange = 0.12
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Min"
            valueText: editorPreview.activeMaskSaturationMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskSaturationMin
            onValueDragged: function(value) { editorPreview.activeMaskSaturationMin = value }
            onResetRequested: editorPreview.activeMaskSaturationMin = 0.1
        }

        InspectorSlider {
            width: parent.width
            label: "Sat Max"
            valueText: editorPreview.activeMaskSaturationMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskSaturationMax
            onValueDragged: function(value) { editorPreview.activeMaskSaturationMax = value }
            onResetRequested: editorPreview.activeMaskSaturationMax = 1.0
        }
    }

    MaskSection {
        width: parent.width
        title: "Depth Range"
        visible: editorPreview.activeMaskKind === "depth"
        expandable: true
        expanded: true

        RowLayout {
            width: parent.width
            spacing: 6

            MiniButton {
                objectName: "activeMaskDepthRangeSampleButton"
                Layout.fillWidth: true
                text: "Sample"
                iconName: "eyedropper"
                role: "segmented"
                active: editRoot.maskDepthRangeSampling && editorPreview.activeMaskKind === "depth"
                tooltip: editorPreview.activeMaskDepthSamplingAvailable
                         ? "Sample a depth value from the image."
                         : editorPreview.activeMaskDepthSamplingDisabledReason
                enabled: editorPreview.hasAsset &&
                         editorPreview.activeMaskDepthSamplingAvailable &&
                         editorPreview.activeMaskDepthSamples.length < 5
                onClicked: editRoot.setMaskDepthRangeSampler(!active)
            }

            Label {
                objectName: "activeMaskDepthRangeSampleCountLabel"
                text: editorPreview.activeMaskDepthSamples.length + "/5"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
                font.letterSpacing: 0
            }
        }

        RowLayout {
            objectName: "activeMaskDepthRangeSampleStrip"
            width: parent.width
            visible: editorPreview.activeMaskDepthSamples.length > 0
            spacing: 5

            Repeater {
                model: editorPreview.activeMaskDepthSamples

                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    readonly property real swatchDepth: Math.max(0, Math.min(1, Number(modelData.depthMean || 0)))
                    spacing: 3

                    Rectangle {
                        objectName: "activeMaskDepthRangeSampleSwatch" + index
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 18
                        radius: 3
                        color: Qt.rgba(parent.swatchDepth, parent.swatchDepth, parent.swatchDepth, 1)
                        border.color: theme.alpha(theme.polar, 0.28)
                        border.width: 1
                    }

                    MiniButton {
                        objectName: "activeMaskDepthRangeRemoveSampleButton" + index
                        square: true
                        iconName: "minus"
                        role: "icon"
                        tooltip: "Remove this depth range sample."
                        onClicked: editorPreview.removeActiveDepthRangeSample(index)
                    }
                }
            }
        }

        InspectorSlider {
            width: parent.width
            label: "Near"
            valueText: editorPreview.activeMaskDepthMin.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskDepthMin
            onValueDragged: function(value) { editorPreview.activeMaskDepthMin = value }
            onResetRequested: editorPreview.activeMaskDepthMin = 0.2
        }

        InspectorSlider {
            width: parent.width
            label: "Far"
            valueText: editorPreview.activeMaskDepthMax.toFixed(2)
            from: 0
            to: 1
            stepSize: 0.01
            value: editorPreview.activeMaskDepthMax
            onValueDragged: function(value) { editorPreview.activeMaskDepthMax = value }
            onResetRequested: editorPreview.activeMaskDepthMax = 0.8
        }

        InspectorSlider {
            width: parent.width
            label: "Feather"
            valueText: editorPreview.activeMaskRangeFeather.toFixed(2)
            from: 0
            to: 0.5
            stepSize: 0.01
            value: editorPreview.activeMaskRangeFeather
            onValueDragged: function(value) { editorPreview.activeMaskRangeFeather = value }
            onResetRequested: editorPreview.activeMaskRangeFeather = 0.1
        }
    }
}
