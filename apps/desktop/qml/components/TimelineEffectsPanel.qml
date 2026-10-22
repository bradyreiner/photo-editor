pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

GlassSurface {
    id: root

    required property var theme
    required property var timelineDocument
    required property var currentClip
    property var animatedClipValueProvider: null
    required property bool collapsed
    required property string activeTab
    required property var effectLibraryModel
    required property var transitionLibraryModel
    property var videoColorSignalMetadata: ({})
    property string colorSignalProcessingWarning: ""
    property var favoriteEffectNames: []
    property var recentEffectNames: []
    property string activeColorGradeClipId: ""
    property string activeColorGradeLayerId: ""
    property string timelineCurveChannel: "RGB"
    property string discoveryCategory: "All"
    property bool compatibleOnly: true
    property string effectPreviewSource: ""
    property var selectedEffectPresetPreview: ({})
    property var presetEffectSelection: ({})
    property string effectPresetNameDraft: ""
    property bool effectPresetReplaceExisting: false
    property real contentScrollPosition: 0
    property bool inspectorContentLoadReady: true
    property bool inspectorSelectionContextAvailable: false
    property bool effectsInspectorLoaded: false
    property bool colorInspectorLoaded: false
    property bool transitionsInspectorLoaded: false
    property bool titleInspectorLoaded: false
    property bool motionInspectorLoaded: false
    property bool audioInspectorLoaded: false
    property bool clipInspectorLoaded: false
    property var effectDisclosureState: ({})
    signal toggleCollapseRequested()
    signal tabChangeRequested(string tab)
    signal toggleKeyframeLanesRequested()
    signal openCurveEditorRequested(string parameter)
    signal openAutoReframeRequested()
    signal focusTimelineRequested()
    signal sequenceSettingsRequested()
    signal projectSettingsRequested()
    signal contentScrollPositionChangeRequested(real position)
    signal effectDisclosureChangeRequested(string effectName, bool expanded)
    signal toggleEffectFavoriteRequested(string effectName)
    signal effectUsed(string effectName)
    signal effectMaskEditingRequested(string effectName)
    signal discoveryCategoryChangeRequested(string category)
    signal compatibleOnlyChangeRequested(bool enabled)

    property string effectSearchText: ""
    property string transitionSearchText: ""
    property string selectedKeyframeParameter: "frameScale"
    property bool keyframeLanesVisible: false
    readonly property var documentController: timelineDocument !== undefined && timelineDocument !== null ? timelineDocument : null
    readonly property var clipState: currentClip !== undefined && currentClip !== null ? currentClip : ({})
    readonly property var effectLibraryItems: effectLibraryModel !== undefined && effectLibraryModel !== null ? effectLibraryModel : []
    readonly property var filteredEffectLibraryItems: buildEffectDiscoveryItems()
    readonly property var transitionLibraryItems: transitionLibraryModel !== undefined && transitionLibraryModel !== null ? transitionLibraryModel : []
    readonly property int selectedClipIndex: documentController !== null ? Number(documentController.currentIndex) : -1
    readonly property bool hasSelection: selectedClipIndex >= 0
    readonly property int selectionCount: documentController !== null
                                                  ? Number(documentController.selectedClipCount || 0) : 0
    readonly property bool hasMultiSelection: hasSelection && selectionCount > 1
    readonly property bool hasSingleSelection: hasSelection && !hasMultiSelection
    readonly property bool isVideoClip: root.hasClip && clipState.mediaType === "video"
    readonly property bool hasCurrentFrameColorSignalMetadata:
        videoColorSignalMetadata.clipId !== undefined
        && String(videoColorSignalMetadata.clipId) === String(clipState.clipId || "")
        && Number(videoColorSignalMetadata.width || 0) > 0
    readonly property bool isTitleClip: root.hasClip
                                        && (clipState.mediaType === "title" || clipState.mediaType === "caption")
    readonly property bool isGraphicShape: root.isTitleClip
                                           && clipState.mediaType === "title"
                                           && String(clipState.titleShapeType || "").length > 0
    readonly property string timelineEffectsPanelStyleContract: "timeline-effects-panel-style:v9|surface:borderless|tabs:primary-three-more-menu|effects:seven-real-search-favorites-recents-categories-compatibility-live-thumbnails-versioned-presets-stack-amount-enable-bypass-reset-remove-clear-per-effect-spatial-mask-feather-invert|motion:collapsed-stabilization-analysis|color:per-clip-camera-input-before-grade-stream-tags-range-matrix|controls:shared|type:shared|spacing:shared|states:no-selection-focused-empty-disabled-modified|layout:video-inspector-preserved"
    readonly property string timelineTitleMotionTruthSignature: "timeline-title-motion:v4|target:title-clips|presets:fade-pop-rise-slide-left-right|lifecycle:entrance-and-exit|animation:editable-clip-keyframes|text-position:anchor-and-transform-keyframes|clear:static-removes-motion-and-transform-keys|preview-export:shared-anchor-transform-path|state:undo-save-reopen"
    readonly property string keyframeToolbarLayoutContract: "timeline-keyframe-toolbar:v1|hierarchy:heading-count-menu|controls:dedicated-row|responsive:no-overlap|accessibility:combined-count"
    readonly property int inspectorRowSpacing: theme.densityCompact ? theme.space2 : theme.space4

    radius: 0
    accent: theme.accentWarm400
    accentRail: false
    elevated: false
    edgeOpacity: 0

    // The controller keeps an empty clip-shaped value while selection is being
    // cleared.  Gate the inspector on the authoritative selection index so
    // stale display fields cannot leave clip controls visible.
    readonly property bool hasClip: hasSingleSelection
                                    && clipState.displayLabel !== undefined
                                    && String(clipState.displayLabel || "").length > 0

    function retainActiveInspector() {
        if (root.collapsed || !root.hasClip)
            return

        switch (String(root.activeTab || "")) {
        case "effects": root.effectsInspectorLoaded = true; break
        case "color": root.colorInspectorLoaded = true; break
        case "transitions": root.transitionsInspectorLoaded = true; break
        case "title": root.titleInspectorLoaded = true; break
        case "motion": root.motionInspectorLoaded = true; break
        case "audio": root.audioInspectorLoaded = true; break
        case "clip": root.clipInspectorLoaded = true; break
        }
    }

    function updateInspectorSelectionContext() {
        if (!root.collapsed && root.hasClip) {
            inspectorSelectionRetirementTimer.stop()
            root.inspectorSelectionContextAvailable = true
            root.retainActiveInspector()
            return
        }
        inspectorSelectionRetirementTimer.restart()
    }

    onActiveTabChanged: root.retainActiveInspector()
    onCollapsedChanged: root.updateInspectorSelectionContext()
    onHasClipChanged: root.updateInspectorSelectionContext()
    Component.onCompleted: root.updateInspectorSelectionContext()
    Timer {
        id: inspectorSelectionRetirementTimer
        interval: 500
        repeat: false
        onTriggered: root.inspectorSelectionContextAvailable = !root.collapsed && root.hasClip
    }

    function currentEffectStack() {
        if (!hasClip)
            return []
        if (clipState.effectDetails)
            return clipState.effectDetails
        const names = clipState.effectStack || []
        let legacy = []
        for (let index = 0; index < names.length; ++index)
            legacy.push({"name": names[index], "enabled": true, "amount": 1, "signedAmount": false})
        return legacy
    }

    function updateEffectMask(effectName, currentMask, changes) {
        if (!documentController || String(effectName || "").length === 0)
            return
        const defaults = {
            "enabled": true,
            "shape": "ellipse",
            "centerX": 0.5,
            "centerY": 0.5,
            "width": 0.65,
            "height": 0.65,
            "feather": 0.04,
            "inverted": false
        }
        const updated = Object.assign({}, defaults, currentMask || ({}), changes || ({}), {"enabled": true})
        documentController.setSelectedClipEffectMask(effectName, updated)
        effectMaskEditingRequested(effectName)
    }

    function isEffectApplied(effectName) {
        const stack = currentEffectStack()
        for (let index = 0; index < stack.length; ++index) {
            if (String(stack[index].name || "") === effectName)
                return true
        }
        return false
    }

    function effectNameInList(effectName, names) {
        return (names || []).indexOf(String(effectName || "")) >= 0
    }

    function isEffectFavorite(effectName) {
        return effectNameInList(effectName, favoriteEffectNames)
    }

    function isEffectCompatible(effect) {
        if (!hasClip)
            return true
        const mediaType = String(clipState.mediaType || "").toLowerCase()
        const supported = effect && effect.compatibleTypes ? effect.compatibleTypes : []
        return supported.length === 0 || supported.indexOf(mediaType) >= 0
    }

    function effectMatchesSearch(effect) {
        const query = String(effectSearchText || "").trim().toLowerCase()
        if (query.length === 0)
            return true
        const searchable = String(effect.name || "") + " "
                + String(effect.category || "") + " "
                + String(effect.description || "") + " "
                + String(effect.keywords || "")
        return searchable.toLowerCase().indexOf(query) >= 0
    }

    function effectMatchesCategory(effect) {
        const effectName = String(effect.name || "")
        if (discoveryCategory === "Favorites")
            return isEffectFavorite(effectName)
        if (discoveryCategory === "Recent")
            return effectNameInList(effectName, recentEffectNames)
        if (discoveryCategory === "All")
            return true
        return String(effect.category || "") === discoveryCategory
    }

    function buildEffectDiscoveryItems() {
        // Keep filtering and sorting bounded to the small catalog; thumbnails
        // are instantiated only for the resulting virtualized ListView rows.
        const source = effectLibraryItems || []
        let result = []
        for (let index = 0; index < source.length; ++index) {
            const effect = source[index]
            if (effectMatchesSearch(effect)
                    && effectMatchesCategory(effect)
                    && (!compatibleOnly || isEffectCompatible(effect)))
                result.push(effect)
        }
        if (discoveryCategory === "Recent") {
            result.sort(function(left, right) {
                return recentEffectNames.indexOf(String(left.name || ""))
                        - recentEffectNames.indexOf(String(right.name || ""))
            })
        }
        return result
    }

    function effectCategoryMenuItems() {
        const categories = ["All", "Favorites", "Recent", "Color", "Style", "Utility"]
        let items = []
        for (let index = 0; index < categories.length; ++index) {
            const category = categories[index]
            items.push({"key": category, "label": category,
                        "iconName": category === "Favorites" ? "star"
                                    : category === "Recent" ? "clock" : "filter",
                        "checked": category === discoveryCategory})
        }
        return items
    }

    function effectPresetItems() {
        return documentController && documentController.effectStackPresets
                ? documentController.effectStackPresets : []
    }

    function effectPresetLabels() {
        const presets = effectPresetItems()
        let labels = []
        for (let index = 0; index < presets.length; ++index) {
            const preset = presets[index]
            labels.push(String(preset.name || "Preset")
                        + (preset.builtIn === true
                           ? " · Looksy"
                           : " · r" + String(preset.revision || 1)
                             + (preset.latest === false ? " · Previous" : "")))
        }
        return labels
    }

    function selectEffectPreset(index) {
        const presets = effectPresetItems()
        if (!documentController || index < 0 || index >= presets.length) {
            selectedEffectPresetPreview = ({})
            presetEffectSelection = ({})
            return
        }
        selectedEffectPresetPreview = documentController.effectStackPresetPreview(
                    String(presets[index].presetId || ""))
        let selection = ({})
        const effects = selectedEffectPresetPreview.effects || []
        for (let effectIndex = 0; effectIndex < effects.length; ++effectIndex) {
            if (effects[effectIndex].available !== false)
                selection[String(effects[effectIndex].name || "")] = true
        }
        presetEffectSelection = selection
    }

    function togglePresetEffect(effectName) {
        const key = String(effectName || "")
        const updated = Object.assign({}, presetEffectSelection)
        updated[key] = updated[key] !== true
        presetEffectSelection = updated
    }

    function selectedPresetEffectNames() {
        const effects = selectedEffectPresetPreview.effects || []
        let selected = []
        for (let index = 0; index < effects.length; ++index) {
            const effectName = String(effects[index].name || "")
            if (effects[index].available !== false && presetEffectSelection[effectName] === true)
                selected.push(effectName)
        }
        return selected
    }

    function openEffectPresetManager() {
        effectPresetNameDraft = ""
        effectPresetReplaceExisting = false
        effectPresetPopup.open()
    }

    function effectAmountText(effect) {
        const amount = Number(effect && effect.amount !== undefined ? effect.amount : 0)
        return (amount > 0 && effect && effect.signedAmount === true ? "+" : "")
                + Math.round(amount * 100) + "%"
    }

    function effectAmountAtPlayheadText(effect) {
        const effectName = String(effect && effect.name || "")
        const baseAmount = effect && effect.baseAmount !== undefined
                           ? effect.baseAmount : effect && effect.amount
        const amount = root.animatedClipValue(
            root.clipState, "effect:" + effectName, baseAmount)
        return (amount > 0 && effect && effect.signedAmount === true ? "+" : "")
                + Math.round(amount * 100) + "%"
    }

    function effectExpanded(effectName) {
        const value = effectDisclosureState[String(effectName || "")]
        return value === undefined ? true : value === true
    }

    function effectStackIndex(effectName) {
        const stack = currentEffectStack()
        for (let index = 0; index < stack.length; ++index) {
            if (String(stack[index].name || "") === String(effectName || ""))
                return index
        }
        return -1
    }

    function addQuickEffect(effectName) {
        if (!hasSelection || documentController === null)
            return
        documentController.addSelectedClipEffect(effectName)
        effectUsed(effectName)
    }

    function assignTransition(edge, transitionName, durationSeconds) {
        if (!hasSelection || documentController === null)
            return
        documentController.assignSelectedClipTransition(edge, transitionName, durationSeconds)
    }

    function beginInspectorEdit() {
        if (documentController !== null)
            documentController.beginContinuousEdit()
    }

    function endInspectorEdit() {
        if (documentController !== null)
            documentController.endContinuousEdit()
    }

    function setStabilizationSettings(enabled, smoothing, cropProtection,
                                      rollingShutterEnabled, rollingShutterAmount) {
        if (documentController === null || !hasSelection)
            return
        documentController.setSelectedClipStabilization(
                    enabled, smoothing, cropProtection,
                    rollingShutterEnabled, rollingShutterAmount)
    }

    function stabilizationSmoothing() {
        return Number(clipState.stabilizationSmoothing !== undefined
                      ? clipState.stabilizationSmoothing : 0.55)
    }

    function stabilizationCropProtection() {
        return Number(clipState.stabilizationCropProtection !== undefined
                      ? clipState.stabilizationCropProtection : 0.5)
    }

    function keyframesForParameter(parameter) {
        const source = root.hasClip && clipState.keyframes ? clipState.keyframes : []
        let values = []
        for (let index = 0; index < source.length; ++index) {
            if (String(source[index].parameter || "") === parameter)
                values.push(source[index])
        }
        values.sort(function(left, right) { return Number(left.seconds || 0) - Number(right.seconds || 0) })
        return values
    }

    function keyframeParameterOptions() {
        return (clipState.availableKeyframeParameters || []).map(parameter => ({
            "parameter": String(parameter),
            "label": colorGradeKeyframeLabel(String(parameter)),
            "keyframeCount": keyframesForParameter(String(parameter)).length
        }))
    }

    onClipStateChanged: {
        const parameters = clipState.availableKeyframeParameters || []
        if (parameters.length && parameters.indexOf(selectedKeyframeParameter) < 0)
            selectedKeyframeParameter = String(parameters[0])
    }

    function currentKeyframeLocalSeconds() {
        if (!root.hasClip || !root.documentController)
            return 0
        return Math.max(0, Math.min(Number(clipState.trimmedDurationSeconds || 0),
                                   Number(root.documentController.sequencePlayheadSeconds || 0)
                                   - Number(clipState.startSeconds || 0)))
    }

    function keyframeAtPlayhead(parameter) {
        const values = keyframesForParameter(parameter)
        const local = currentKeyframeLocalSeconds()
        const tolerance = 0.5 / Math.max(1, Number(root.documentController
                                                   ? root.documentController.framesPerSecond : 30))
        for (let index = 0; index < values.length; ++index) {
            if (Math.abs(Number(values[index].seconds || 0) - local) <= tolerance)
                return values[index]
        }
        return null
    }

    function interpolationLabel(value) {
        if (value === "hold") return "Hold"
        if (value === "ease-in") return "Ease In"
        if (value === "ease-out") return "Ease Out"
        if (value === "ease-in-out") return "Ease In Out"
        if (value === "bezier") return "Bezier"
        return "Linear"
    }

    function interpolationValue(label) {
        const normalized = String(label || "").toLowerCase()
        if (normalized === "ease in") return "ease-in"
        if (normalized === "ease out") return "ease-out"
        if (normalized === "ease in out") return "ease-in-out"
        return normalized
    }

    component ParameterKeyframeButton: ActionButton {
        required property string parameterName
        readonly property var playheadKeyframe: root.keyframeAtPlayhead(parameterName)
        readonly property string parameterLabel: root.colorGradeKeyframeLabel(parameterName)
        showText: false
        compact: true
        iconName: playheadKeyframe ? "keyframe-filled" : "keyframe"
        active: playheadKeyframe !== null
        enabled: root.hasSelection
        tooltipText: playheadKeyframe
                     ? "Remove the " + parameterLabel + " keyframe at the playhead."
                     : "Add a " + parameterLabel + " keyframe at the playhead."
        onClicked: {
            root.selectedKeyframeParameter = parameterName
            if (root.documentController)
                root.documentController.toggleSelectedClipKeyframe(parameterName)
        }
    }

    function updateTitle(values) {
        if (root.isTitleClip && root.documentController)
            root.documentController.updateSelectedTitle(values)
    }

    function animatedClipValue(clip, parameter, fallbackValue) {
        if (typeof root.animatedClipValueProvider === "function")
            return Number(root.animatedClipValueProvider(clip, parameter, fallbackValue))
        return Number(fallbackValue || 0)
    }

    function defaultTitlePositionY() {
        switch (String(clipState.titleTemplateKey || "basic")) {
        case "headline": return 0.28
        case "lower-third": return 0.82
        case "callout": return 0.35
        case "quote":
        case "brand-card": return 0.5
        default: return 0.5
        }
    }

    function titlePositionValue(axis) {
        const horizontal = String(axis || "x") === "x"
        const titleProperty = horizontal ? "titlePositionX" : "titlePositionY"
        const offsetProperty = horizontal ? "frameOffsetX" : "frameOffsetY"
        const defaultValue = horizontal ? 0.5 : root.defaultTitlePositionY()
        const anchor = root.animatedClipValue(
            clipState,
            titleProperty,
            clipState[titleProperty] !== undefined ? clipState[titleProperty] : defaultValue)
        const offset = root.animatedClipValue(
            clipState,
            offsetProperty,
            clipState[offsetProperty] !== undefined ? clipState[offsetProperty] : 0)
        return Math.max(0, Math.min(1, anchor + offset * 0.25))
    }

    function setTitlePosition(axis, value) {
        if (!root.isTitleClip || !root.documentController)
            return
        const horizontal = String(axis || "x") === "x"
        const offsetProperty = horizontal ? "frameOffsetX" : "frameOffsetY"
        const boundedPosition = Math.max(0, Math.min(1, Number(value)))
        const offset = root.animatedClipValue(
            clipState,
            offsetProperty,
            clipState[offsetProperty] !== undefined ? clipState[offsetProperty] : 0)
        const anchor = Math.max(0, Math.min(1, boundedPosition - offset * 0.25))
        if (horizontal)
            root.documentController.setSelectedTitlePositionX(anchor)
        else
            root.documentController.setSelectedTitlePositionY(anchor)
    }

    function resetTitlePosition(axis) {
        const horizontal = String(axis || "x") === "x"
        root.setTitlePosition(axis, horizontal ? 0.5 : root.defaultTitlePositionY())
    }

    function captionEmphasisModeIndex() {
        const modes = ["none", "highlight", "karaoke", "pop", "bounce", "single-word"]
        const index = modes.indexOf(String(root.clipState.captionEmphasisMode || "highlight"))
        return index >= 0 ? index : 1
    }

    function captionEmphasisModeAt(index) {
        return ["none", "highlight", "karaoke", "pop", "bounce", "single-word"]
                [Math.max(0, Math.min(5, Number(index || 0)))]
    }

    function setCaptionEmphasis(mode, reducedMotion, applyToAll) {
        if (root.documentController)
            root.documentController.setSelectedCaptionEmphasis(
                        mode, reducedMotion, applyToAll)
    }

    function titleTemplateMenuItems() {
        const templates = root.documentController ? root.documentController.titleTemplates : []
        const activeKey = String(root.clipState.titleTemplateKey || "custom")
        let items = []
        for (let index = 0; index < templates.length; ++index) {
            const template = templates[index]
            items.push({
                "key": String(template.key || ""),
                "label": String(template.label || "Title"),
                "iconName": "text",
                "checked": String(template.key || "") === activeKey
            })
        }
        return items
    }

    function activeTitleTemplateLabel() {
        const templates = root.documentController ? root.documentController.titleTemplates : []
        const activeKey = String(root.clipState.titleTemplateKey || "custom")
        for (let index = 0; index < templates.length; ++index) {
            if (String(templates[index].key || "") === activeKey)
                return String(templates[index].label || "Title Style")
        }
        return "Custom Style"
    }

    function inspectorMoreMenuItems() {
        return [
            {"key": "transitions", "label": "Transitions", "iconName": "transition", "checked": activeTab === "transitions"},
            {"key": "color", "label": "Color", "iconName": "swatch", "checked": activeTab === "color",
             "enabled": root.hasClip
                        && (clipState.mediaType === "video" || clipState.mediaType === "adjustment"),
             "disabledReason": "Select a video or adjustment clip to edit its shared photo grade."},
            {"key": "clip", "label": "Clip details", "iconName": "info", "checked": activeTab === "clip"},
            {"key": "title", "label": "Title and captions", "iconName": "text", "checked": activeTab === "title", "enabled": root.isTitleClip,
             "disabledReason": "Select a title or caption clip to edit text."}
        ]
    }

    function handleInspectorMoreAction(key) {
        tabChangeRequested(key)
    }

    function videoInputTransformMenuItems() {
        const selected = String(clipState.videoInputTransform || "")
        const automatic = String(clipState.videoInputTransformSource || "automatic") === "automatic"
        return [
            {"key": "auto", "label": "Automatic from camera metadata", "checked": automatic},
            {"key": "none", "label": "No camera input transform", "checked": !automatic && selected.length === 0},
            {"key": "slog2-sgamut", "label": "Sony S-Log2 / S-Gamut to Rec.709",
             "checked": !automatic && selected === "slog2-sgamut"},
            {"key": "slog3-sgamut3-cine", "label": "Sony S-Log3 / S-Gamut3.Cine to Rec.709",
             "checked": !automatic && selected === "slog3-sgamut3-cine"},
            {"key": "slog3-sgamut3", "label": "Sony S-Log3 / S-Gamut3 to Rec.709",
             "checked": !automatic && selected === "slog3-sgamut3"}
        ]
    }

    function videoInputTransformButtonLabel() {
        const source = String(clipState.videoInputTransformSource || "automatic")
        const selected = String(clipState.videoInputTransform || "")
        if (source === "automatic")
            return "Automatic"
        if (selected.length === 0 || selected === "none")
            return "No transform"
        if (selected === "slog2-sgamut")
            return "Sony S-Log2"
        if (selected === "slog3-sgamut3-cine")
            return "Sony S-Log3 · Cine"
        if (selected === "slog3-sgamut3")
            return "Sony S-Log3"
        return "Manual transform"
    }

    function videoInputTransformTooltip() {
        const profileName = String(clipState.videoInputTransformName || "")
        const source = String(clipState.videoInputTransformSource || "automatic")
        const current = profileName.length > 0 ? profileName
                      : source === "automatic" ? "Automatic from camera metadata"
                      : String(clipState.videoInputTransform || "No camera input transform")
        return current + "\nChoose the camera transfer curve and gamut recorded by this clip."
    }

    function setVideoInputTransform(profile) {
        if (documentController)
            documentController.setSelectedClipVideoInputTransform(String(profile || "none"))
    }

    function colorGradeLayers() {
        return hasClip ? (clipState.colorGradeLayers || []) : []
    }

    function colorGradeBlendModes() {
        return ["normal", "multiply", "screen", "overlay", "soft-light", "color", "luminosity"]
    }

    function colorGradeBlendLabels() {
        return ["Normal", "Multiply", "Screen", "Overlay", "Soft Light", "Color", "Luminosity"]
    }

    function colorGradeBlendModeIndex(modeName) {
        let normalized = String(modeName || "normal").trim().toLowerCase()
        if (normalized === "soft light" || normalized === "softlight")
            normalized = "soft-light"
        const index = colorGradeBlendModes().indexOf(normalized)
        return index >= 0 ? index : 0
    }

    function colorGradeBlendModeAt(index) {
        const modes = colorGradeBlendModes()
        return modes[Math.max(0, Math.min(modes.length - 1, Number(index || 0)))]
    }

    function activeColorGradeLayer() {
        if (!hasClip || activeColorGradeClipId !== String(clipState.clipId || "")
                || !activeColorGradeLayerId)
            return null
        const layers = colorGradeLayers()
        for (let index = 0; index < layers.length; ++index) {
            if (String(layers[index].id || "") === activeColorGradeLayerId)
                return layers[index]
        }
        return null
    }

    function selectColorGradeLayer(layerId) {
        activeColorGradeClipId = hasClip ? String(clipState.clipId || "") : ""
        activeColorGradeLayerId = String(layerId || "")
    }

    function editColorGradeLayer(operation, layerId, options) {
        if (!documentController || !hasClip)
            return ({})
        const args = options || ({})
        return documentController.editClipColorGradeLayerForAutomation(
                    String(clipState.clipId || ""), String(operation || ""),
                    String(layerId || ""), args.layer || ({}),
                    args.position === undefined ? -1 : Number(args.position),
                    Boolean(args.enabled), args.opacity === undefined ? 1 : Number(args.opacity),
                    String(args.parameter || ""), Number(args.value || 0),
                    String(args.blendMode || ""))
    }

    function addColorGradeLayer() {
        if (!documentController || !hasClip)
            return
        const result = documentController.addClipColorGradeAdjustmentLayerForAutomation(
                    String(clipState.clipId || ""),
                    "Adjustment " + String(colorGradeLayers().length + 1))
        if (result && result.ok)
            selectColorGradeLayer(String(result.layerId || ""))
    }

    function colorGradeParameter(name) {
        const layer = activeColorGradeLayer()
        if (layer)
            return Number(layer[name] || 0)
        const parameters = clipState.colorGradeParameters || ({})
        return Number(parameters[name] || 0)
    }

    function colorGradeCurvePoints(channel) {
        const layer = activeColorGradeLayer()
        const curves = layer ? (layer.colorGradeCurves || ({}))
                             : (clipState.colorGradeCurves || ({}))
        const points = curves[String(channel || "RGB")]
        if (points && points.length >= 2)
            return points
        return [{"x": 0, "y": 0}, {"x": 1, "y": 1}]
    }

    function setColorGradeCurvePoints(channel, points) {
        if (!documentController || !hasClip)
            return
        const layer = activeColorGradeLayer()
        documentController.setClipColorGradeCurveForAutomation(
                    String(clipState.clipId || ""),
                    layer ? String(layer.id || "") : "",
                    String(channel || "RGB"), points || [])
    }

    function colorGradeKeyframeParameter(name) {
        const layer = activeColorGradeLayer()
        return layer
                ? "grade:layer:" + String(layer.id || "") + ":" + String(name || "")
                : "grade:" + String(name || "")
    }

    function colorGradeKeyframeLabel(name) {
        const labels = {"frameScale": "Scale", "frameOffsetX": "Horizontal Position",
                        "frameOffsetY": "Vertical Position", "frameRotationDegrees": "Rotation",
                        "framePerspectiveX": "Perspective X", "framePerspectiveY": "Perspective Y",
                        "titleFontSize": "Title Size", "titlePositionX": "Title Horizontal Position",
                        "titlePositionY": "Title Vertical Position", "opacity": "Opacity",
                        "audioGainDb": "Audio Gain", "sourceTime": "Source Time"}
        if (labels[name]) return labels[name]
        if (String(name || "").indexOf("effect:") === 0)
            return String(name).substring(7) + " Amount"
        let layerName = "Adjustment Layer"
        let control = String(name || "")
        const layerGrade = control.indexOf("grade:layer:") === 0
        const baseGrade = control.indexOf("grade:") === 0 && !layerGrade
        if (layerGrade) {
            const separator = control.indexOf(":", 12)
            if (separator > 12) {
                const layerId = control.substring(12, separator)
                for (const layer of clipState.colorGradeLayers || []) {
                    if (String(layer.id) === layerId) layerName = String(layer.name || layerName)
                }
                control = control.substring(separator + 1)
            }
        } else if (baseGrade) {
            control = control.substring(6)
        }
        control = control.replace(/([a-z])([A-Z])/g, "$1 $2")
        if (layerGrade)
            return layerName + " · " + control
        return baseGrade ? "Grade " + control : control
    }

    function setColorGradeParameter(name, value) {
        if (!documentController || !hasClip)
            return
        const layer = activeColorGradeLayer()
        if (layer) {
            const parameter = colorGradeKeyframeParameter(name)
            if (keyframesForParameter(parameter).length > 0) {
                documentController.editClipKeyframeForAutomation(
                            String(clipState.clipId || ""), "set", parameter,
                            currentKeyframeLocalSeconds(), Number(value), "")
                return
            }
            if (String(name || "") === "opacity") {
                editColorGradeLayer("set_opacity", String(layer.id || ""),
                                    {"opacity": Number(value)})
                return
            }
            editColorGradeLayer("set_parameter", String(layer.id || ""),
                                {"parameter": String(name || ""), "value": Number(value)})
            return
        }
        const parameter = "grade:" + String(name || "")
        if (keyframesForParameter(parameter).length > 0) {
            documentController.editClipKeyframeForAutomation(
                        String(clipState.clipId || ""), "set", parameter,
                        currentKeyframeLocalSeconds(), Number(value), "")
            return
        }
        documentController.setClipColorGradeParameterForAutomation(
                    String(clipState.clipId || ""), String(name || ""), Number(value))
    }

    function multiSelectionMenuItems() {
        return [
            {"key": "copy", "label": "Copy selected clips", "iconName": "copy"},
            {"key": "lift", "label": "Lift selected clips", "iconName": "delete"},
            {"key": "extract", "label": "Extract and close gaps", "iconName": "ripple"}
        ]
    }

    function handleMultiSelectionAction(key) {
        if (!root.documentController)
            return
        if (key === "copy")
            root.documentController.copySelectedClips()
        else if (key === "lift")
            root.documentController.liftSelectedClips()
        else if (key === "extract")
            root.documentController.extractSelectedClips()
    }

    function keyframeMoreMenuItems() {
        const count = root.keyframesForParameter(root.selectedKeyframeParameter).length
        const clipboardCount = root.documentController
                ? Number(root.documentController.keyframeClipboardCount || 0) : 0
        return [
            {"key": "toggle-lanes", "label": root.keyframeLanesVisible ? "Hide keyframe lanes" : "Show keyframe lanes",
             "iconName": "keyframe", "checked": root.keyframeLanesVisible},
            {"key": "copy-track", "label": "Copy parameter keyframes", "iconName": "copy", "enabled": count > 0,
             "disabledReason": "Add keyframes to this parameter first."},
            {"key": "paste-track", "label": "Paste at playhead", "iconName": "copy", "enabled": clipboardCount > 0,
             "disabledReason": "Copy keyframes first."},
            {"key": "timing-half", "label": "Compress timing to 0.5x", "iconName": "step-back", "enabled": count > 1,
             "disabledReason": "At least two keyframes are required."},
            {"key": "timing-double", "label": "Expand timing to 2x", "iconName": "step-forward", "enabled": count > 1,
             "disabledReason": "At least two keyframes are required."},
            {"key": "preset-slow-zoom", "label": "Preset · Slow zoom", "iconName": "motion"},
            {"key": "preset-zoom-out", "label": "Preset · Zoom out", "iconName": "motion"},
            {"key": "preset-social-pop", "label": "Preset · Social pop", "iconName": "motion"},
            {"key": "preset-drift-left", "label": "Preset · Drift left", "iconName": "motion"},
            {"key": "preset-drift-right", "label": "Preset · Drift right", "iconName": "motion"},
            {"key": "preset-drift-up", "label": "Preset · Drift up", "iconName": "motion"},
            {"key": "preset-fade-in", "label": "Preset · Fade in", "iconName": "visible"},
            {"key": "preset-fade-out", "label": "Preset · Fade out", "iconName": "hidden"}
        ]
    }

    function handleKeyframeMoreAction(key) {
        if (!root.documentController)
            return
        if (key === "toggle-lanes") {
            root.toggleKeyframeLanesRequested()
        } else if (key === "copy-track") {
            root.documentController.copySelectedClipKeyframes(root.selectedKeyframeParameter)
        } else if (key === "paste-track") {
            root.documentController.pasteSelectedClipKeyframes(
                        root.currentKeyframeLocalSeconds(), root.selectedKeyframeParameter)
        } else if (key === "timing-half") {
            root.documentController.scaleSelectedClipKeyframeTimes(root.selectedKeyframeParameter, 0.5)
        } else if (key === "timing-double") {
            root.documentController.scaleSelectedClipKeyframeTimes(root.selectedKeyframeParameter, 2.0)
        } else if (String(key).indexOf("preset-") === 0) {
            root.documentController.applySelectedClipMotionPreset(String(key).slice(7))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.panelPaddingCompact
        spacing: theme.gapDense

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.gapDense

            Label {
                visible: !collapsed
                text: root.hasMultiSelection
                      ? root.selectionCount + " clips"
                      : root.hasClip ? String(clipState.displayLabel || "Clip") : "Inspector"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                objectName: "timelineInspectorCollapseButton"
                iconName: collapsed ? "chevron-left" : "chevron-right"
                showText: false
                compact: true
                tooltipText: collapsed ? "Expand inspector" : "Collapse inspector"
                onClicked: toggleCollapseRequested()
            }
        }

        ColumnLayout {
            visible: collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: theme.gapDense

            ActionButton {
                objectName: "timelineInspectorEffectsRailButton"
                Layout.fillWidth: true
                iconName: "effects"
                showText: false
                compact: true
                tooltipText: "Effects"
                enabled: root.hasClip
                disabledReason: root.hasMultiSelection ? "Batch effects are not available." : "Select one clip."
                active: activeTab === "effects"
                onClicked: tabChangeRequested("effects")
            }

            ActionButton {
                objectName: "timelineInspectorTransitionsRailButton"
                Layout.fillWidth: true
                iconName: "transition"
                showText: false
                compact: true
                tooltipText: "Transitions"
                enabled: root.hasClip
                disabledReason: root.hasMultiSelection ? "Select one edit point." : "Select one clip."
                active: activeTab === "transitions"
                onClicked: tabChangeRequested("transitions")
            }

            ActionButton {
                objectName: "timelineInspectorMotionRailButton"
                Layout.fillWidth: true
                iconName: "motion"
                showText: false
                compact: true
                tooltipText: "Motion"
                enabled: root.hasClip
                disabledReason: root.hasMultiSelection ? "Batch motion is not available." : "Select one clip."
                active: activeTab === "motion"
                onClicked: tabChangeRequested("motion")
            }

            ActionButton {
                objectName: "timelineInspectorAudioRailButton"
                Layout.fillWidth: true
                iconName: "audio"
                showText: false
                compact: true
                tooltipText: "Audio"
                enabled: root.hasClip
                disabledReason: root.hasMultiSelection ? "Batch audio is not available." : "Select one clip."
                active: activeTab === "audio"
                onClicked: tabChangeRequested("audio")
            }

            ActionButton {
                objectName: "timelineInspectorTitleRailButton"
                Layout.fillWidth: true
                iconName: "text"
                showText: false
                compact: true
                tooltipText: "Title"
                enabled: root.isTitleClip
                disabledReason: "Select a title or caption clip to edit text."
                active: activeTab === "title"
                onClicked: tabChangeRequested("title")
            }

            Item { Layout.fillHeight: true }
        }

        ColumnLayout {
            visible: !collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: theme.gapDense

            Item {
                objectName: "timelineInspectorNoSelectionState"
                visible: !root.hasSelection
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, theme.scaled(250))
                    spacing: theme.gapRelated

                    AppIcon {
                        Layout.alignment: Qt.AlignHCenter
                        name: "select"
                        color: theme.textMuted
                        implicitWidth: theme.space20
                        implicitHeight: theme.space20
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Select a clip to inspect"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeControl
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Effects, motion, and audio controls appear for the selected timeline item."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }

                    ActionButton {
                        objectName: "timelineInspectorFocusTimelineButton"
                        Layout.alignment: Qt.AlignHCenter
                        text: "Focus Timeline"
                        iconName: "timeline"
                        compact: true
                        tooltipText: "Move keyboard focus to the timeline tracks."
                        onClicked: root.focusTimelineRequested()
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: theme.space6

                        ActionButton {
                            objectName: "timelineInspectorSequenceSettingsButton"
                            text: "Sequence"
                            iconName: "fit"
                            compact: true
                            tooltipText: "Adjust the sequence format and output framing."
                            onClicked: root.sequenceSettingsRequested()
                        }

                        ActionButton {
                            objectName: "timelineInspectorProjectSettingsButton"
                            text: "Project"
                            iconName: "settings"
                            compact: true
                            tooltipText: "Open application and project preferences."
                            onClicked: root.projectSettingsRequested()
                        }
                    }
                }
            }

            Item {
                objectName: "timelineInspectorMultiSelectionState"
                visible: root.hasMultiSelection
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, theme.scaled(260))
                    spacing: theme.gapRelated

                    AppIcon {
                        Layout.alignment: Qt.AlignHCenter
                        name: "layers"
                        color: theme.accentCool400
                        implicitWidth: theme.space20
                        implicitHeight: theme.space20
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.selectionCount + " clips selected"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeControl
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Batch selection · only shared actions are shown"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: theme.space6

                        ActionButton {
                            text: "Create Compound"
                            iconName: "layers"
                            compact: true
                            tooltipText: "Nest the selected clips into one editable compound clip."
                            onClicked: root.documentController.createCompoundClipFromSelection("")
                        }

                        MenuButton {
                            text: "More"
                            compact: true
                            primaryOpensMenu: true
                            menuItems: root.multiSelectionMenuItems()
                            menuWidth: 224
                            onMenuItemTriggered: function(key) {
                                root.handleMultiSelectionAction(key)
                            }
                        }
                    }

                    ActionButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Focus Timeline"
                        iconName: "timeline"
                        compact: true
                        onClicked: root.focusTimelineRequested()
                    }
                }
            }

            RowLayout {
                visible: root.hasClip
                Layout.fillWidth: true
                spacing: theme.gapDense

                FilterChip {
                    objectName: "timelineInspectorEffectsTab"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 54
                    text: "Effects"
                    active: activeTab === "effects"
                    onClicked: tabChangeRequested("effects")
                }

                FilterChip {
                    objectName: "timelineInspectorMotionTab"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 54
                    text: "Motion"
                    active: activeTab === "motion"
                    onClicked: tabChangeRequested("motion")
                }

                FilterChip {
                    objectName: "timelineInspectorAudioTab"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 54
                    text: "Audio"
                    active: activeTab === "audio"
                    onClicked: tabChangeRequested("audio")
                }

                MenuButton {
                    objectName: "timelineInspectorMoreMenu"
                    Layout.minimumWidth: 66
                    text: "More"
                    compact: true
                    active: activeTab === "transitions" || activeTab === "color"
                            || activeTab === "clip" || activeTab === "title"
                    primaryOpensMenu: true
                    tooltipText: "Color input, transitions, clip details, and title controls."
                    menuItems: root.inspectorMoreMenuItems()
                    menuWidth: 210
                    onMenuItemTriggered: function(key) { root.handleInspectorMoreAction(key) }
                }
            }

            Loader {
                objectName: "timelineEffectsInspectorLoader"
                property bool keepActiveThroughIncubation: false
                active: root.inspectorContentLoadReady
                        && (keepActiveThroughIncubation
                            || (root.inspectorSelectionContextAvailable
                                && (root.activeTab === "effects" || root.effectsInspectorLoaded)))
                visible: root.hasClip && root.activeTab === "effects"
                Layout.fillWidth: true
                Layout.fillHeight: visible
                onStatusChanged: {
                    const loading = status === Loader.Loading
                    Qt.callLater(function() { keepActiveThroughIncubation = loading })
                }
                onLoaded: root.effectsInspectorLoaded = true

                sourceComponent: Component {
                    Flickable {
                        id: effectsInspectorFlick
                        objectName: "timelineEffectsInspector"
                        contentWidth: width
                        contentHeight: Math.max(height, effectsInspectorContent.implicitHeight
                                                + theme.panelPaddingCompact * 2)
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        ScrollBar.vertical: ScrollBar { }

                        Shortcut {
                            sequences: [StandardKey.Find]
                            enabled: root.visible && !root.collapsed && root.activeTab === "effects"
                            onActivated: {
                                effectsInspectorFlick.contentY = Math.max(0, Math.min(
                                    effectSearchField.mapToItem(effectsInspectorContent, 0, 0).y,
                                    effectsInspectorFlick.contentHeight - effectsInspectorFlick.height))
                                effectSearchField.forceActiveFocus()
                                effectSearchField.selectAll()
                            }
                        }

                        ColumnLayout {
                            id: effectsInspectorContent
                            x: theme.panelPaddingCompact
                            y: theme.panelPaddingCompact
                            width: parent.width - theme.panelPaddingCompact * 2
                            height: Math.max(implicitHeight, parent.height - theme.panelPaddingCompact * 2)
                            spacing: theme.gapDense

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                Label {
                                    text: "APPLIED EFFECTS"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: theme.typeLabel
                                    font.weight: Font.DemiBold
                                    Layout.minimumWidth: implicitWidth
                                    Layout.preferredWidth: implicitWidth
                                    Accessible.name: text
                                }

                                Label {
                                    text: String(root.currentEffectStack().length)
                                    color: theme.textFaint
                                    font.family: theme.fontMono
                                    font.pixelSize: theme.typeCaption
                                    Accessible.name: text + " effects in the stack"
                                }

                                Item { Layout.fillWidth: true }

                                ActionButton {
                                    objectName: "timelineQuickEffectPickerButton"
                                    text: qsTr("Add Effect")
                                    showText: true
                                    iconName: "plus"
                                    compact: true
                                    visible: root.effectLibraryItems.length > 0
                                    enabled: root.effectLibraryItems.length > 0
                                    Accessible.name: qsTr("Add effect")
                                    tooltipText: "Search and add an effect without leaving the applied stack."
                                    onClicked: quickEffectPicker.open()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                Item { Layout.fillWidth: true }

                                ActionButton {
                                    objectName: "timelineEffectStackPresetsButton"
                                    text: qsTr("Looks")
                                    iconName: "layers"
                                    showText: true
                                    compact: true
                                    enabled: root.isVideoClip
                                    disabledReason: "Effect stack looks apply to video clips."
                                    tooltipText: "Save, preview, version, or selectively apply an effect stack."
                                    onClicked: root.openEffectPresetManager()
                                }

                                ConfirmActionButton {
                                    visible: root.currentEffectStack().length > 0
                                    iconName: "delete"
                                    showText: false
                                    compact: true
                                    fillButtonWidth: false
                                    text: "Clear Stack"
                                    confirmText: "Clear all effects"
                                    helperText: "Press again to remove every effect from this clip"
                                    tooltipText: "Clear all effects from this clip."
                                    onConfirmed: if (root.documentController)
                                                     root.documentController.clearSelectedClipEffects()
                                }

                                ActionButton {
                                    visible: root.currentEffectStack().length > 0
                                    text: root.hasClip && clipState.effectsEnabled === false ? "Bypassed" : "Enabled"
                                    compact: true
                                    active: root.hasClip && clipState.effectsEnabled !== false
                                    tooltipText: root.hasClip && clipState.effectsEnabled === false
                                                 ? "Enable the complete effect stack."
                                                 : "Temporarily bypass the complete effect stack."
                                    onClicked: if (root.documentController)
                                                   root.documentController.setSelectedClipEffectsEnabled(
                                                       root.hasClip && clipState.effectsEnabled === false)
                                }
                            }

                            ColumnLayout {
                                id: stackColumn
                                objectName: "timelineAppliedEffectsStack"
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                Repeater {
                                    model: root.currentEffectStack()

                                    delegate: Rectangle {
                                        required property var modelData
                                        readonly property string effectName: String(modelData.name || "")
                                        readonly property bool effectEnabled: modelData.enabled !== false
                                        readonly property bool expandedState: root.effectExpanded(effectName)
                                        readonly property int stackIndex: root.effectStackIndex(effectName)
                                        readonly property int effectKeyframeCount:
                                            root.keyframesForParameter("effect:" + effectName).length
                                        objectName: "timelineAppliedEffect-" + effectName
                                        Layout.fillWidth: true
                                        implicitHeight: effectControls.implicitHeight + theme.sectionPaddingCompact * 2
                                        radius: 0
                                        color: theme.alpha(theme.ink850, effectEnabled ? 0.28 : 0.14)
                                        border.width: 0

                                        ColumnLayout {
                                            id: effectControls
                                            anchors.fill: parent
                                            anchors.margins: theme.sectionPaddingCompact
                                            spacing: theme.gapDense

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: theme.gapDense

                                                Label {
                                                    Layout.fillWidth: true
                                                    text: effectName
                                                    color: effectEnabled ? theme.textStrong : theme.textMuted
                                                    font.family: theme.fontUi
                                                    font.pixelSize: theme.typeBody
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }

                                                Label {
                                                    visible: modelData.modified === true
                                                    text: "Modified"
                                                    color: theme.accentCool300
                                                    font.family: theme.fontMono
                                                    font.pixelSize: theme.typeCaption
                                                }

                                                Label {
                                                    objectName: "timelineEffectAmountSummary-" + effectName
                                                    visible: !expandedState
                                                    text: root.effectAmountAtPlayheadText(modelData)
                                                    color: theme.textMain
                                                    font.family: theme.fontMono
                                                    font.pixelSize: theme.typeCaption
                                                    Accessible.name: effectName + " amount at playhead " + text
                                                }

                                                ActionButton {
                                                    objectName: "timelineEffectCurveButton-" + effectName
                                                    visible: effectKeyframeCount > 0
                                                    text: String(effectKeyframeCount)
                                                    iconName: "curve"
                                                    compact: true
                                                    enabled: effectEnabled && root.hasSelection
                                                    disabledReason: "Enable this effect to edit its animation curve."
                                                    Accessible.name: qsTr("Edit %1 amount curve, %2 keyframes")
                                                                       .arg(effectName).arg(effectKeyframeCount)
                                                    tooltipText: qsTr("Open the %1 amount curve with %2 keyframes.")
                                                                  .arg(effectName).arg(effectKeyframeCount)
                                                    onClicked: root.openCurveEditorRequested("effect:" + effectName)
                                                }

                                                ActionButton {
                                                    objectName: "timelineEffectBypass-" + effectName
                                                    text: effectEnabled ? "On" : "Off"
                                                    compact: true
                                                    active: effectEnabled
                                                    tooltipText: effectEnabled ? "Bypass this effect." : "Enable this effect."
                                                    onClicked: if (root.documentController)
                                                                   root.documentController.setSelectedClipEffectEnabled(
                                                                       effectName, !effectEnabled)
                                                }

                                                ActionButton {
                                                    objectName: "timelineEffectDisclosure-" + effectName
                                                    showText: false
                                                    iconName: expandedState ? "chevron-up" : "chevron-down"
                                                    compact: true
                                                    tooltipText: expandedState ? "Collapse effect controls"
                                                                               : "Expand effect controls"
                                                    onClicked: root.effectDisclosureChangeRequested(
                                                                   effectName, !expandedState)
                                                }
                                            }

                                            RowLayout {
                                                visible: expandedState
                                                Layout.fillWidth: true
                                                spacing: theme.space4

                                                InspectorSlider {
                                                    Layout.fillWidth: true
                                                    label: "Amount"
                                                    valueText: root.effectAmountText(modelData)
                                                    from: modelData.signedAmount === true ? -1 : 0
                                                    to: 1
                                                    value: Number(modelData.amount || 0)
                                                    defaultValue: effectName === "Exposure"
                                                                  || effectName === "Contrast"
                                                                  || effectName === "Saturation" ? 0.25
                                                                  : effectName === "Blur" ? 0.30
                                                                  : effectName === "Fade" || effectName === "Punch" ? 0.50 : 1
                                                    valueInputMode: "percentNormalized"
                                                    enabled: effectEnabled && root.hasSelection
                                                    onInteractionStarted: root.beginInspectorEdit()
                                                    onInteractionFinished: root.endInspectorEdit()
                                                    onValueDragged: function(value) {
                                                        if (root.documentController)
                                                            root.documentController.setSelectedClipEffectAmount(effectName, value)
                                                    }
                                                    onResetRequested: if (root.documentController)
                                                                          root.documentController.resetSelectedClipEffect(effectName)
                                                }

                                                ParameterKeyframeButton {
                                                    parameterName: "effect:" + effectName
                                                    enabled: effectEnabled && root.hasSelection
                                                }
                                            }

                                            ColumnLayout {
                                                visible: expandedState
                                                         && (String(root.clipState.mediaType || "") === "video"
                                                             || String(root.clipState.mediaType || "") === "photo")
                                                Layout.fillWidth: true
                                                spacing: theme.space4
                                                readonly property var maskState: modelData.mask || ({})
                                                readonly property bool maskEnabled: maskState.enabled === true

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: theme.gapDense

                                                    ActionButton {
                                                        objectName: effectName === "Blur"
                                                                    ? "timelineBlurMaskToggle"
                                                                    : "timelineEffectMaskToggle-" + effectName
                                                        text: parent.parent.maskEnabled ? "Clear Mask" : "Add Mask"
                                                        compact: true
                                                        active: parent.parent.maskEnabled
                                                        tooltipText: parent.parent.maskEnabled
                                                                     ? "Remove this effect's spatial mask."
                                                                     : "Limit " + effectName + " to a movable shape mask."
                                                        onClicked: {
                                                            if (!root.documentController)
                                                                return
                                                            if (parent.parent.maskEnabled)
                                                                root.documentController.clearSelectedClipEffectMask(effectName)
                                                            else
                                                                root.updateEffectMask(effectName, parent.parent.maskState, ({}))
                                                            root.effectMaskEditingRequested(effectName)
                                                        }
                                                    }

                                                    ActionButton {
                                                        objectName: effectName === "Blur"
                                                                    ? "timelineBlurMaskShape"
                                                                    : "timelineEffectMaskShape-" + effectName
                                                        text: String(parent.parent.maskState.shape || "ellipse") === "rectangle"
                                                              ? "Rectangle" : "Ellipse"
                                                        compact: true
                                                        enabled: parent.parent.maskEnabled && root.hasSelection
                                                        tooltipText: "Switch the " + effectName + " mask between ellipse and rectangle."
                                                        onClicked: root.updateEffectMask(effectName, parent.parent.maskState,
                                                            {"shape": String(parent.parent.maskState.shape || "ellipse") === "rectangle"
                                                                ? "ellipse" : "rectangle"})
                                                    }

                                                    ActionButton {
                                                        objectName: effectName === "Blur"
                                                                    ? "timelineBlurMaskInvert"
                                                                    : "timelineEffectMaskInvert-" + effectName
                                                        text: parent.parent.maskState.inverted === true ? "Inverted" : "Invert"
                                                        compact: true
                                                        active: parent.parent.maskState.inverted === true
                                                        enabled: parent.parent.maskEnabled && root.hasSelection
                                                        tooltipText: "Apply " + effectName + " outside the shape."
                                                        onClicked: root.updateEffectMask(effectName, parent.parent.maskState,
                                                            {"inverted": parent.parent.maskState.inverted !== true})
                                                    }
                                                }

                                                RowLayout {
                                                    visible: parent.maskEnabled
                                                    Layout.fillWidth: true
                                                    spacing: theme.gapDense

                                                    InspectorSlider {
                                                        Layout.fillWidth: true
                                                        label: "Center X"
                                                        valueText: Math.round(Number(parent.parent.maskState.centerX || 0) * 100) + "%"
                                                        from: 0
                                                        to: 1
                                                        value: Number(parent.parent.maskState.centerX !== undefined
                                                                      ? parent.parent.maskState.centerX : 0.5)
                                                        valueInputMode: "percentNormalized"
                                                        enabled: root.hasSelection
                                                        onInteractionStarted: root.beginInspectorEdit()
                                                        onInteractionFinished: root.endInspectorEdit()
                                                        onValueDragged: function(value) {
                                                            root.updateEffectMask(effectName, parent.parent.maskState, {"centerX": value})
                                                        }
                                                    }

                                                    InspectorSlider {
                                                        Layout.fillWidth: true
                                                        label: "Center Y"
                                                        valueText: Math.round(Number(parent.parent.maskState.centerY || 0) * 100) + "%"
                                                        from: 0
                                                        to: 1
                                                        value: Number(parent.parent.maskState.centerY !== undefined
                                                                      ? parent.parent.maskState.centerY : 0.5)
                                                        valueInputMode: "percentNormalized"
                                                        enabled: root.hasSelection
                                                        onInteractionStarted: root.beginInspectorEdit()
                                                        onInteractionFinished: root.endInspectorEdit()
                                                        onValueDragged: function(value) {
                                                            root.updateEffectMask(effectName, parent.parent.maskState, {"centerY": value})
                                                        }
                                                    }
                                                }

                                                RowLayout {
                                                    visible: parent.maskEnabled
                                                    Layout.fillWidth: true
                                                    spacing: theme.gapDense

                                                    InspectorSlider {
                                                        Layout.fillWidth: true
                                                        label: "Width"
                                                        valueText: Math.round(Number(parent.parent.maskState.width || 0.65) * 100) + "%"
                                                        from: 0.05
                                                        to: 1
                                                        value: Number(parent.parent.maskState.width || 0.65)
                                                        valueInputMode: "percentNormalized"
                                                        enabled: root.hasSelection
                                                        onInteractionStarted: root.beginInspectorEdit()
                                                        onInteractionFinished: root.endInspectorEdit()
                                                        onValueDragged: function(value) {
                                                            root.updateEffectMask(effectName, parent.parent.maskState, {"width": value})
                                                        }
                                                    }

                                                    InspectorSlider {
                                                        Layout.fillWidth: true
                                                        label: "Height"
                                                        valueText: Math.round(Number(parent.parent.maskState.height || 0.65) * 100) + "%"
                                                        from: 0.05
                                                        to: 1
                                                        value: Number(parent.parent.maskState.height || 0.65)
                                                        valueInputMode: "percentNormalized"
                                                        enabled: root.hasSelection
                                                        onInteractionStarted: root.beginInspectorEdit()
                                                        onInteractionFinished: root.endInspectorEdit()
                                                        onValueDragged: function(value) {
                                                            root.updateEffectMask(effectName, parent.parent.maskState, {"height": value})
                                                        }
                                                    }
                                                }

                                                RowLayout {
                                                    visible: parent.maskEnabled
                                                    Layout.fillWidth: true

                                                    InspectorSlider {
                                                        Layout.fillWidth: true
                                                        label: "Feather"
                                                        valueText: Math.round(Number(parent.parent.maskState.feather || 0) * 100) + "%"
                                                        from: 0
                                                        to: 0.5
                                                        value: Number(parent.parent.maskState.feather || 0)
                                                        valueInputMode: "percentNormalized"
                                                        enabled: root.hasSelection
                                                        onInteractionStarted: root.beginInspectorEdit()
                                                        onInteractionFinished: root.endInspectorEdit()
                                                        onValueDragged: function(value) {
                                                            root.updateEffectMask(effectName, parent.parent.maskState, {"feather": value})
                                                        }
                                                    }
                                                }
                                            }

                                            RowLayout {
                                                visible: expandedState
                                                Layout.fillWidth: true
                                                spacing: theme.gapDense

                                                ActionButton {
                                                    objectName: "timelineEffectMoveUp-" + effectName
                                                    showText: false
                                                    iconName: "chevron-up"
                                                    compact: true
                                                    enabled: stackIndex > 0
                                                    disabledReason: "This effect is already first."
                                                    tooltipText: "Move effect earlier"
                                                    onClicked: if (root.documentController)
                                                                   root.documentController.moveSelectedClipEffect(
                                                                       effectName, -1)
                                                }

                                                ActionButton {
                                                    objectName: "timelineEffectMoveDown-" + effectName
                                                    showText: false
                                                    iconName: "chevron-down"
                                                    compact: true
                                                    enabled: stackIndex >= 0
                                                             && stackIndex < root.currentEffectStack().length - 1
                                                    disabledReason: "This effect is already last."
                                                    tooltipText: "Move effect later"
                                                    onClicked: if (root.documentController)
                                                                   root.documentController.moveSelectedClipEffect(
                                                                       effectName, 1)
                                                }

                                                Item { Layout.fillWidth: true }

                                                ActionButton {
                                                    text: "Reset"
                                                    iconName: "reset"
                                                    compact: true
                                                    tooltipText: "Restore this effect's default amount."
                                                    onClicked: if (root.documentController)
                                                                   root.documentController.resetSelectedClipEffect(effectName)
                                                }

                                                ConfirmActionButton {
                                                    text: "Remove"
                                                    compact: true
                                                    fillButtonWidth: false
                                                    enabled: root.hasSelection
                                                    helperText: "Remove this effect from the selected clip"
                                                    onConfirmed: if (root.documentController)
                                                                     root.documentController.removeSelectedClipEffect(effectName)
                                                }
                                            }
                                        }
                                    }
                                }

                                Label {
                                    visible: root.currentEffectStack().length === 0
                                    text: "No effects applied"
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeBody
                                    font.weight: Font.Medium
                                }
                            }

                            RowLayout {
                                visible: root.effectLibraryItems.length > 0
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                Label {
                                    Layout.fillWidth: true
                                    text: "AVAILABLE EFFECTS"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: theme.typeLabel
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    text: String(root.filteredEffectLibraryItems.length)
                                    color: theme.textFaint
                                    font.family: theme.fontMono
                                    font.pixelSize: theme.typeCaption
                                    Accessible.name: text + " matching effects"
                                }

                                MenuButton {
                                    objectName: "timelineEffectCategoryMenu"
                                    text: root.discoveryCategory
                                    iconName: root.discoveryCategory === "Favorites" ? "star"
                                              : root.discoveryCategory === "Recent" ? "clock" : "filter"
                                    compact: true
                                    primaryOpensMenu: true
                                    menuItems: root.effectCategoryMenuItems()
                                    tooltipText: "Browse effects by category, favorites, or recent use."
                                    onMenuItemTriggered: function(key) {
                                        root.discoveryCategoryChangeRequested(key)
                                    }
                                }
                            }

                            RowLayout {
                                visible: root.effectLibraryItems.length > 0
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                ToneTextField {
                                    id: effectSearchField
                                    objectName: "timelineEffectKeyboardSearch"
                                    Layout.fillWidth: true
                                    text: root.effectSearchText
                                    placeholderText: "Search effects"
                                    selectByMouse: true
                                    Accessible.description: "Search effect names, categories, descriptions, and keywords."
                                    onTextChanged: root.effectSearchText = text
                                    Keys.onDownPressed: function(event) {
                                        if (effectLibraryList.count > 0) {
                                            effectLibraryList.currentIndex = 0
                                            effectLibraryList.currentItem.forceActiveFocus()
                                            event.accepted = true
                                        }
                                    }
                                }

                                FilterChip {
                                    objectName: "timelineEffectCompatibilityFilter"
                                    text: "Compatible"
                                    active: root.compatibleOnly
                                    Accessible.description: "Show only effects supported by the selected media type."
                                    onClicked: root.compatibleOnlyChangeRequested(!root.compatibleOnly)
                                }
                            }

                            Label {
                                visible: root.effectLibraryItems.length > 0
                                         && root.filteredEffectLibraryItems.length === 0
                                Layout.fillWidth: true
                                text: root.discoveryCategory === "Favorites"
                                      ? "No favorite effects yet. Use the star on any effect to keep it here."
                                      : root.discoveryCategory === "Recent"
                                        ? "Recently added effects appear here."
                                        : "No effects match the current search and compatibility filters."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.Wrap
                            }

                            ListView {
                                id: effectLibraryList
                                objectName: "timelineEffectLibraryList"
                                visible: root.effectLibraryItems.length > 0
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumHeight: Math.max(theme.scaled(96),
                                    Math.min(theme.scaled(180), effectsInspectorFlick.height
                                             - theme.panelPaddingCompact * 2))
                                clip: true
                                reuseItems: true
                                cacheBuffer: Math.max(height, theme.scaled(320))
                                spacing: theme.gapDense
                                model: root.filteredEffectLibraryItems
                                contentY: Math.max(0, Math.min(root.contentScrollPosition,
                                                              Math.max(0, contentHeight - height)))
                                onContentYChanged: {
                                    if (visible && moving && Math.abs(root.contentScrollPosition - contentY) > 0.5)
                                        root.contentScrollPositionChangeRequested(contentY)
                                }

                                delegate: TimelineEffectLibraryRow {
                                    previewSource: root.effectPreviewSource
                                    favorite: root.isEffectFavorite(effectName)
                                    compatible: root.isEffectCompatible(modelData)
                                    recent: root.effectNameInList(effectName, root.recentEffectNames)
                                    actionEnabled: root.hasSelection && compatible
                                                   && !root.isEffectApplied(effectName)
                                    actionLabel: root.isEffectApplied(effectName) ? "Added" : "Add"
                                    onAddRequested: function(effectName) { root.addQuickEffect(effectName) }
                                    onFavoriteToggleRequested: function(effectName) {
                                        root.toggleEffectFavoriteRequested(effectName)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Loader {
                objectName: "timelineVideoColorInspectorLoader"
                property bool keepActiveThroughIncubation: false
                active: root.inspectorContentLoadReady
                        && (keepActiveThroughIncubation
                            || (root.inspectorSelectionContextAvailable
                                && (root.activeTab === "color" || root.colorInspectorLoaded)))
                visible: root.hasClip && (root.clipState.mediaType === "video" || root.clipState.mediaType === "adjustment") && root.activeTab === "color"
                Layout.fillWidth: true
                Layout.fillHeight: visible
                onStatusChanged: {
                    const loading = status === Loader.Loading
                    Qt.callLater(function() { keepActiveThroughIncubation = loading })
                }
                onLoaded: root.colorInspectorLoaded = true

                sourceComponent: Component {
                    Item {
                        objectName: "timelineVideoColorInspector"

                        ScrollView {
                            id: timelineVideoColorInspectorScroll
                            objectName: "timelineVideoColorInspectorScroll"
                            anchors.fill: parent
                            clip: true
                            contentWidth: availableWidth
                            contentHeight: videoColorInspectorColumn.y
                                           + videoColorInspectorColumn.implicitHeight
                                           + theme.panelPaddingCompact
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }

                            ColumnLayout {
                                id: videoColorInspectorColumn
                                x: theme.panelPaddingCompact
                                y: theme.panelPaddingCompact
                                width: Math.max(1, timelineVideoColorInspectorScroll.availableWidth
                                                - theme.panelPaddingCompact * 2)
                            spacing: theme.gapDense

                            InspectorSection {
                                Layout.fillWidth: true
                                visible: clipState.mediaType === "video"
                                title: "Camera input transform"
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: clipState.mediaType === "video"
                                text: "Applies before clip effects and finishing LUTs."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeLabel
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: clipState.mediaType === "video"
                                         && root.colorSignalProcessingWarning.length > 0
                                text: root.colorSignalProcessingWarning
                                color: theme.warning
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeLabel
                                wrapMode: Text.Wrap
                            }

                            MenuButton {
                                objectName: "timelineVideoInputTransformMenu"
                                Layout.fillWidth: true
                                visible: clipState.mediaType === "video"
                                text: root.videoInputTransformButtonLabel()
                                primaryOpensMenu: true
                                tooltipText: root.videoInputTransformTooltip()
                                menuItems: root.videoInputTransformMenuItems()
                                menuWidth: Math.min(theme.menuMaxWidth,
                                                    Math.max(theme.menuMinWidth,
                                                             root.width - theme.panelPaddingCompact * 4))
                                onMenuItemTriggered: function(key) { root.setVideoInputTransform(key) }
                            }

                            StatusChip {
                                objectName: "timelineVideoInputTransformStatus"
                                Layout.alignment: Qt.AlignLeft
                                visible: clipState.mediaType === "video"
                                text: clipState.videoInputTransform
                                      ? (clipState.videoInputTransformSource === "automatic"
                                         ? "Camera metadata profile · Rec.709 output"
                                         : "Manual camera profile · Rec.709 output")
                                      : (clipState.videoInputTransformSource === "automatic"
                                         ? (clipState.mediaAnalysisStatus === "failed"
                                            ? "Automatic · source metadata unavailable"
                                            : clipState.mediaAnalysisStatus === "ready"
                                              ? "Automatic · no camera match"
                                              : "Automatic · reading source tags…")
                                         : "Manual bypass · no input transform")
                                tooltipText: clipState.videoInputTransform
                                             ? (clipState.videoInputTransformSource === "automatic"
                                                ? "Camera metadata profile · Rec.709 output"
                                                : "Manual camera profile · Rec.709 output")
                                             : (clipState.videoInputTransformSource === "automatic"
                                                ? (clipState.mediaAnalysisStatus === "failed"
                                                   ? "Automatic · source metadata unavailable"
                                                   : clipState.mediaAnalysisStatus === "ready"
                                                     ? "Automatic · no exact camera metadata match"
                                                     : "Automatic · reading source tags…")
                                                : "Manual bypass · no input transform")
                                maximumReadableWidth: Math.max(theme.scaled(112),
                                                              root.width - theme.panelPaddingCompact * 4)
                                tone: clipState.videoInputTransform
                                      ? "info" : "neutral"
                                compact: true
                            }

                            InspectorSection {
                                objectName: "timelineVideoInputTransformDetails"
                                Layout.fillWidth: true
                                visible: clipState.mediaType === "video"
                                title: "Input details"
                                expandable: true
                                expanded: false

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.gapDense

                                    Flow {
                                        Layout.fillWidth: true
                                        visible: clipState.mediaType === "video"
                                        spacing: theme.gapDense

                                        StatusChip {
                                            visible: root.hasCurrentFrameColorSignalMetadata
                                            text: "Transfer · " + String(root.videoColorSignalMetadata.transfer || "Unknown")
                                            tone: String(root.videoColorSignalMetadata.transfer || "Unknown") === "Unknown"
                                                  ? "neutral" : "info"
                                            compact: true
                                        }

                                        StatusChip {
                                            visible: root.hasCurrentFrameColorSignalMetadata
                                            text: String(root.videoColorSignalMetadata.range || "Unknown range")
                                            tone: "neutral"
                                            compact: true
                                        }

                                        StatusChip {
                                            visible: root.hasCurrentFrameColorSignalMetadata
                                            text: "Color space · "
                                                  + String(root.videoColorSignalMetadata.colorSpace || "Unknown")
                                            tone: "neutral"
                                            compact: true
                                        }

                                        StatusChip {
                                            visible: root.hasCurrentFrameColorSignalMetadata
                                            text: Number(root.videoColorSignalMetadata.maxLuminanceNits || 0) > 0
                                                  ? "Peak · "
                                                    + Number(root.videoColorSignalMetadata.maxLuminanceNits).toFixed(0)
                                                    + " nits"
                                                  : "Peak · Unknown"
                                            tone: "neutral"
                                            compact: true
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: clipState.mediaType === "video"
                                        text: root.hasCurrentFrameColorSignalMetadata
                                              ? "Program frame " + Number(root.videoColorSignalMetadata.width)
                                                + "×" + Number(root.videoColorSignalMetadata.height || 0)
                                                + " · " + String(root.videoColorSignalMetadata.pixelFormat || "Unknown format")
                                              : "Decoder-reported stream tags appear while this clip is the current Program frame."
                                        color: theme.textFaint
                                        font.family: theme.fontMono
                                        font.pixelSize: theme.typeCaption
                                        wrapMode: Text.Wrap
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        visible: clipState.mediaType === "video"
                                        text: "Automatic matching requires the exact Sony Gamma and Color for Look tags; generic transfer and primaries tags are not enough."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeCaption
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: clipState.mediaType === "video"
                                text: "Shared by timeline preview and delivery. Build Final Preview to inspect rendered pixels."
                                color: theme.textFaint
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeLabel
                                wrapMode: Text.Wrap
                            }

                            InspectorSection {
                                Layout.fillWidth: true
                                title: clipState.mediaType === "adjustment"
                                       ? "Grade underlying picture"
                                       : "Shared photo grade"
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: clipState.mediaType === "adjustment"
                                text: "This grade is composited over every eligible picture track below this adjustment clip."
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeLabel
                                wrapMode: Text.Wrap
                            }

                            StatusChip {
                                Layout.alignment: Qt.AlignLeft
                                text: clipState.colorGradeSequenceExportPending
                                      ? "Checking grade delivery…"
                                      : clipState.colorGradeRecipeEnabled
                                      ? (clipState.colorGradeSequenceExportSupported
                                         ? (clipState.colorGradeSequenceExportUsesFrameStream
                                            ? "Photo engine · frame-stream delivery"
                                            : "Photo engine · preview + CUBE delivery")
                                         : "Photo engine · full-graph preview")
                                      : "Photo engine · Rec.709"
                                tone: clipState.colorGradeSequenceExportPending
                                      ? "neutral"
                                      : clipState.colorGradeRecipeEnabled
                                      ? (clipState.colorGradeSequenceExportSupported ? "info" : "warning")
                                      : "neutral"
                                compact: true
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: Boolean(clipState.colorGradeRecipeEnabled
                                                 && !clipState.colorGradeSequenceExportSupported)
                                text: String(clipState.colorGradeSequenceExportError ||
                                             "This recipe uses spatial work. Preview uses the full photo pipeline; sequence export needs a spatial grade placement supported by the shared full-frame route.")
                                color: clipState.colorGradeSequenceExportPending ? theme.textMuted : theme.warning
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeLabel
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: String(clipState.colorGradeRecipeError || "").length > 0
                                text: String(clipState.colorGradeRecipeError || "")
                                color: theme.error
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeLabel
                                wrapMode: Text.Wrap
                            }

                            InspectorSection {
                                Layout.fillWidth: true
                                title: "Adjustment layers"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                MiniButton {
                                    Layout.fillWidth: true
                                    text: "Base grade"
                                    role: "segmented"
                                    active: !root.activeColorGradeLayer()
                                    tooltip: "Edit the clip's base photo-engine grade."
                                    onClicked: root.selectColorGradeLayer("")
                                }

                                MiniButton {
                                    objectName: "timelineAddColorGradeLayer"
                                    text: "Add layer"
                                    role: "secondary"
                                    enabled: root.hasClip
                                             && String(root.clipState.colorGradeRecipeError || "").length === 0
                                    onClicked: root.addColorGradeLayer()
                                }
                            }

                            Repeater {
                                model: root.colorGradeLayers()

                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    readonly property bool selectedLayer:
                                        root.activeColorGradeClipId === String(root.clipState.clipId || "")
                                        && root.activeColorGradeLayerId === String(modelData.id || "")

                                    Layout.fillWidth: true
                                    implicitHeight: layerRow.implicitHeight + 8
                                    radius: 0
                                    color: selectedLayer
                                           ? theme.alpha(theme.selectionAccent, 0.13)
                                           : theme.alpha(theme.ink850, 0.48)
                                    border.width: selectedLayer ? 1 : 0
                                    border.color: theme.selectionAccent

                                    RowLayout {
                                        id: layerRow
                                        anchors.fill: parent
                                        anchors.margins: 4
                                        spacing: theme.gapDense

                                        MiniButton {
                                            Layout.fillWidth: true
                                            text: String(modelData.name || ("Adjustment " + (index + 1)))
                                            role: "ghost"
                                            active: selectedLayer
                                            tooltip: "Select this adjustment layer for grading."
                                            onClicked: root.selectColorGradeLayer(String(modelData.id || ""))
                                        }

                                        ToneCheckBox {
                                            objectName: "timelineColorGradeLayerEnabled"
                                            accessibleLabel: "Enable " + String(modelData.name || "adjustment layer")
                                            checked: modelData.enabled !== false
                                            onToggled: root.editColorGradeLayer(
                                                           "set_enabled", String(modelData.id || ""),
                                                           {"enabled": checked})
                                        }

                                        MiniButton {
                                            iconName: "move-up"
                                            labelVisible: false
                                            square: true
                                            role: "ghost"
                                            a11yName: "Move layer up"
                                            tooltip: "Move adjustment layer toward the top of the stack."
                                            enabled: index > 0
                                            onClicked: root.editColorGradeLayer(
                                                           "move", String(modelData.id || ""),
                                                           {"position": index - 1})
                                        }

                                        MiniButton {
                                            iconName: "move-down"
                                            labelVisible: false
                                            square: true
                                            role: "ghost"
                                            a11yName: "Move layer down"
                                            tooltip: "Move adjustment layer toward the base of the stack."
                                            enabled: index + 1 < root.colorGradeLayers().length
                                            onClicked: root.editColorGradeLayer(
                                                           "move", String(modelData.id || ""),
                                                           {"position": index + 1})
                                        }

                                        MiniButton {
                                            text: "Remove"
                                            role: "destructive"
                                            tooltip: "Remove this adjustment layer."
                                            onClicked: {
                                                if (selectedLayer)
                                                    root.selectColorGradeLayer("")
                                                root.editColorGradeLayer(
                                                            "remove", String(modelData.id || ""), ({}))
                                            }
                                        }
                                    }
                                }
                            }

                            InspectorSection {
                                Layout.fillWidth: true
                                title: "Tone curves"
                            }

                            ToneCurveEditor {
                                objectName: "timelineToneCurveEditor"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 208
                                compactMode: true
                                selectedChannel: root.timelineCurveChannel
                                pointsModel: root.colorGradeCurvePoints(root.timelineCurveChannel)
                                enabled: root.hasClip
                                         && String(root.clipState.colorGradeRecipeError || "").length === 0
                                onEditStarted: root.beginInspectorEdit()
                                onEditFinished: root.endInspectorEdit()
                                onChannelSelected: function(channel) {
                                    root.timelineCurveChannel = String(channel || "RGB")
                                }
                                onPointsEdited: function(points) {
                                    root.setColorGradeCurvePoints(root.timelineCurveChannel, points)
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: !root.activeColorGradeLayer()
                                text: "The sliders below edit the base grade. Select an adjustment layer to target its own photo-engine settings."
                                color: theme.textFaint
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeLabel
                                wrapMode: Text.Wrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: root.activeColorGradeLayer() !== null
                                spacing: theme.gapDense

                                InspectorSlider {
                                    objectName: "timelineColorGradeLayerOpacity"
                                    Layout.fillWidth: true
                                    label: "Layer opacity"
                                    from: 0
                                    to: 1
                                    value: root.activeColorGradeLayer()
                                           ? Number(root.activeColorGradeLayer().opacity === undefined
                                                    ? 1 : root.activeColorGradeLayer().opacity)
                                           : 1
                                    defaultValue: 1
                                    stepSize: 0.01
                                    precisionResponse: true
                                    valueText: Math.round(value * 100) + "%"
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(nextValue) {
                                        root.setColorGradeParameter("opacity", nextValue)
                                    }
                                    onResetRequested: root.setColorGradeParameter("opacity", 1)
                                }

                                ParameterKeyframeButton {
                                    parameterName: root.colorGradeKeyframeParameter("opacity")
                                    enabled: root.hasClip
                                             && root.activeColorGradeLayer() !== null
                                             && String(root.clipState.colorGradeRecipeError || "").length === 0
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                visible: root.activeColorGradeLayer() !== null
                                spacing: theme.gapDense

                                Label {
                                    Layout.preferredWidth: 84
                                    text: "Blend mode"
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeLabel
                                }

                                ToneComboBox {
                                    objectName: "timelineColorGradeBlendMode"
                                    Layout.fillWidth: true
                                    accessibleLabel: "Adjustment layer blend mode"
                                    model: root.colorGradeBlendLabels()
                                    currentIndex: root.colorGradeBlendModeIndex(
                                                      root.activeColorGradeLayer()
                                                      ? root.activeColorGradeLayer().blendMode
                                                      : "normal")
                                    onActivated: function(index) {
                                        const layer = root.activeColorGradeLayer()
                                        if (layer) {
                                            root.editColorGradeLayer(
                                                        "set_blend_mode", String(layer.id || ""),
                                                        {"blendMode": root.colorGradeBlendModeAt(index)})
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: [
                                    {"key": "exposure", "label": "Exposure", "minimum": -5, "maximum": 5},
                                    {"key": "contrast", "label": "Contrast", "minimum": -100, "maximum": 100},
                                    {"key": "highlights", "label": "Highlights", "minimum": -100, "maximum": 100},
                                    {"key": "shadows", "label": "Shadows", "minimum": -100, "maximum": 100},
                                    {"key": "whites", "label": "Whites", "minimum": -100, "maximum": 100},
                                    {"key": "blacks", "label": "Blacks", "minimum": -100, "maximum": 100},
                                    {"key": "temperature", "label": "Temperature", "minimum": -100, "maximum": 100},
                                    {"key": "tint", "label": "Tint", "minimum": -100, "maximum": 100},
                                    {"key": "vibrance", "label": "Vibrance", "minimum": -100, "maximum": 100},
                                    {"key": "saturation", "label": "Saturation", "minimum": -100, "maximum": 100}
                                ]

                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: root.inspectorRowSpacing

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: modelData.label
                                        from: modelData.minimum
                                        to: modelData.maximum
                                        value: root.colorGradeParameter(modelData.key)
                                        defaultValue: 0
                                        stepSize: modelData.key === "exposure" ? 0.01 : 1
                                        precisionResponse: modelData.key === "exposure"
                                        valueInputMode: modelData.key === "exposure" ? "raw" : "percentNormalized"
                                        valueText: modelData.key === "exposure"
                                                   ? Number(value).toFixed(2) + " EV"
                                                   : String(Math.round(value))
                                        enabled: root.hasClip
                                                 && String(root.clipState.colorGradeRecipeError || "").length === 0
                                        disabledReason: String(root.clipState.colorGradeRecipeError || "")
                                        onInteractionStarted: root.beginInspectorEdit()
                                        onInteractionFinished: root.endInspectorEdit()
                                        onValueDragged: function(nextValue) {
                                            root.setColorGradeParameter(modelData.key, nextValue)
                                        }
                                        onResetRequested: root.setColorGradeParameter(modelData.key, 0)
                                    }

                                    ParameterKeyframeButton {
                                        parameterName: root.colorGradeKeyframeParameter(modelData.key)
                                        enabled: root.hasClip
                                                 && root.clipState.colorGradeRecipeEnabled === true
                                                 && String(root.clipState.colorGradeRecipeError || "").length === 0
                                        disabledReason: "Add a shared photo grade or select one of its adjustment layers."
                                    }
                                }
                            }

                            RowLayout {
                                objectName: "timelineThreeWayGradeWheels"
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                Repeater {
                                    model: [
                                        {"key": "gradingShadows", "zone": "Shadows"},
                                        {"key": "gradingMidtones", "zone": "Midtones"},
                                        {"key": "gradingHighlights", "zone": "Highlights"}
                                    ]

                                    delegate: ColumnLayout {
                                        required property var modelData
                                        objectName: "timeline" + modelData.zone + "GradeWheel"
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 82
                                        Layout.preferredWidth: 94
                                        spacing: theme.space4

                                        ColorWheelControl {
                                            Layout.fillWidth: true
                                            title: modelData.zone
                                            wheelSize: 72
                                            hue: root.colorGradeParameter(modelData.key + "Hue")
                                            saturation: root.colorGradeParameter(modelData.key + "Saturation")
                                            luminance: root.colorGradeParameter(modelData.key + "Luminance")
                                            enabled: root.hasClip
                                                     && String(root.clipState.colorGradeRecipeError || "").length === 0
                                            onInteractionStarted: root.beginInspectorEdit()
                                            onInteractionFinished: root.endInspectorEdit()
                                            onHueEdited: function(nextValue) {
                                                root.setColorGradeParameter(modelData.key + "Hue", nextValue)
                                            }
                                            onSaturationEdited: function(nextValue) {
                                                root.setColorGradeParameter(modelData.key + "Saturation", nextValue)
                                            }
                                            onLuminanceEdited: function(nextValue) {
                                                root.setColorGradeParameter(modelData.key + "Luminance", nextValue)
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: theme.space4
                                            visible: true

                                            ParameterKeyframeButton {
                                                Layout.fillWidth: true
                                                parameterName: root.colorGradeKeyframeParameter(modelData.key + "Hue")
                                                enabled: root.hasClip
                                                         && root.clipState.colorGradeRecipeEnabled === true
                                                         && String(root.clipState.colorGradeRecipeError || "").length === 0
                                            }
                                            ParameterKeyframeButton {
                                                Layout.fillWidth: true
                                                parameterName: root.colorGradeKeyframeParameter(modelData.key + "Saturation")
                                                enabled: root.hasClip
                                                         && root.clipState.colorGradeRecipeEnabled === true
                                                         && String(root.clipState.colorGradeRecipeError || "").length === 0
                                            }
                                            ParameterKeyframeButton {
                                                Layout.fillWidth: true
                                                parameterName: root.colorGradeKeyframeParameter(modelData.key + "Luminance")
                                                enabled: root.hasClip
                                                         && root.clipState.colorGradeRecipeEnabled === true
                                                         && String(root.clipState.colorGradeRecipeError || "").length === 0
                                            }
                                        }
                                    }
                                }
                            }
                            }
                        }
                    }
                }
            }

            Loader {
                objectName: "timelineTransitionsInspectorLoader"
                property bool keepActiveThroughIncubation: false
                active: root.inspectorContentLoadReady
                        && (keepActiveThroughIncubation
                            || (root.inspectorSelectionContextAvailable
                                && (root.activeTab === "transitions" || root.transitionsInspectorLoaded)))
                visible: root.hasClip && root.activeTab === "transitions"
                Layout.fillWidth: true
                Layout.fillHeight: visible
                onStatusChanged: {
                    const loading = status === Loader.Loading
                    Qt.callLater(function() { keepActiveThroughIncubation = loading })
                }
                onLoaded: root.transitionsInspectorLoaded = true

                sourceComponent: Component {
                    Item {
                        objectName: "timelineTransitionsInspector"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.panelPaddingCompact
                            spacing: theme.gapDense

                            InspectorSection {
                                Layout.fillWidth: true
                                title: "Transitions"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                Label {
                                    text: "In  •  " + (root.hasClip && clipState.transitionInName ? clipState.transitionInName : "none")
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: theme.typeLabel
                                }

                                Item { Layout.fillWidth: true }

                                ActionButton {
                                    text: "Clear In"
                                    compact: true
                                    enabled: root.hasSelection
                                    disabledReason: "Select a clip to clear its incoming transition."
                                    onClicked: if (root.documentController) root.documentController.clearSelectedClipTransition("in")
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.gapDense

                                Label {
                                    text: "Out  •  " + (root.hasClip && clipState.transitionOutName ? clipState.transitionOutName : "none")
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: theme.typeLabel
                                }

                                Item { Layout.fillWidth: true }

                                ActionButton {
                                    text: "Clear Out"
                                    compact: true
                                    enabled: root.hasSelection
                                    disabledReason: "Select a clip to clear its outgoing transition."
                                    onClicked: if (root.documentController) root.documentController.clearSelectedClipTransition("out")
                                }
                            }

                            ToneTextField {
                                visible: root.transitionLibraryItems.length > 0
                                Layout.fillWidth: true
                                text: root.transitionSearchText
                                placeholderText: "Search transitions"
                                selectByMouse: true
                                onTextChanged: root.transitionSearchText = text
                            }

                            ListView {
                                id: transitionLibraryList
                                objectName: "timelineTransitionLibraryList"
                                visible: root.transitionLibraryItems.length > 0
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                reuseItems: true
                                cacheBuffer: Math.max(height, theme.scaled(320))
                                spacing: theme.gapDense
                                model: root.transitionLibraryItems
                                contentY: Math.max(0, Math.min(root.contentScrollPosition,
                                                              Math.max(0, contentHeight - height)))
                                onContentYChanged: {
                                    if (visible && moving && Math.abs(root.contentScrollPosition - contentY) > 0.5)
                                        root.contentScrollPositionChangeRequested(contentY)
                                }

                                delegate: TimelineTransitionLibraryRow {
                                    searchText: root.transitionSearchText
                                    actionEnabled: root.hasSelection
                                    onTransitionRequested: function(edge, transitionName, durationSeconds) {
                                        root.assignTransition(edge, transitionName, durationSeconds)
                                    }
                                }
                            }

                            Label {
                                visible: root.transitionLibraryItems.length === 0
                                Layout.fillWidth: true
                                text: "Transitions are hidden until they render identically in preview and export."
                                color: theme.warning
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeLabel
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            Loader {
                objectName: "timelineTitleInspectorLoader"
                property bool keepActiveThroughIncubation: false
                active: root.inspectorContentLoadReady
                        && (keepActiveThroughIncubation
                            || (root.inspectorSelectionContextAvailable
                                && (root.activeTab === "title" || root.titleInspectorLoaded)))
                visible: root.hasClip && root.activeTab === "title"
                Layout.fillWidth: true
                Layout.fillHeight: visible
                onStatusChanged: {
                    const loading = status === Loader.Loading
                    Qt.callLater(function() { keepActiveThroughIncubation = loading })
                }
                onLoaded: root.titleInspectorLoaded = true

                sourceComponent: Component {
                    Item {
                        objectName: root.hasClip && clipState.mediaType === "caption"
                                    ? "timelineCaptionInspector" : "timelineTitleInspector"

                        Flickable {
                            id: titleFlick
                            objectName: "timelineTitleCaptionScrollView"
                            anchors.fill: parent
                            anchors.margins: theme.panelPaddingCompact
                            contentHeight: titleEditor.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true
                            contentY: Math.max(0, Math.min(root.contentScrollPosition,
                                                          Math.max(0, contentHeight - height)))
                            onContentYChanged: {
                                if (visible && moving && Math.abs(root.contentScrollPosition - contentY) > 0.5)
                                    root.contentScrollPositionChangeRequested(contentY)
                            }

                            ColumnLayout {
                                id: titleEditor
                                width: parent.width
                                spacing: theme.gapControl

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: root.isTitleClip
                                           ? (clipState.mediaType === "caption" ? "Caption" : "Title")
                                           : "Select a title or caption clip"
                                }

                                RowLayout {
                                    visible: root.isTitleClip && clipState.mediaType === "title"
                                    Layout.fillWidth: true
                                    spacing: theme.gapDense

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Template"
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeLabel
                                    }

                                    MenuButton {
                                        objectName: "timelineTitleTemplateMenuButton"
                                        text: root.isGraphicShape ? "Graphic shape" : root.activeTitleTemplateLabel()
                                        iconName: "text"
                                        compact: true
                                        enabled: root.isTitleClip && clipState.mediaType === "title"
                                        primaryOpensMenu: true
                                        tooltipText: "Restyle this title while preserving its text."
                                        menuItems: root.titleTemplateMenuItems()
                                        menuWidth: 220
                                        onMenuItemTriggered: function(key) {
                                            if (root.documentController)
                                                root.documentController.applySelectedTitleTemplate(key)
                                        }
                                    }
                                }

                                ToneTextArea {
                                    objectName: "timelineTitleTextField"
                                    visible: !root.isGraphicShape
                                    accessibleLabel: "Timeline title text"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 74
                                    text: root.isTitleClip ? String(clipState.titleText || "") : ""
                                    placeholderText: "Title text"
                                    enabled: root.isTitleClip
                                    onActiveFocusChanged: if (!activeFocus) root.updateTitle({"text": text})
                                }

                                InspectorSection {
                                    objectName: "timelineGraphicShapeControls"
                                    visible: root.isGraphicShape
                                    Layout.fillWidth: true
                                    title: "Shape"
                                    subtitle: "Editable overlay geometry and fill"
                                    expanded: true
                                    showAccentMarker: false

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: theme.space6

                                        ToneComboBox {
                                            objectName: "timelineGraphicShapeTypePicker"
                                            Layout.fillWidth: true
                                            model: ["Rectangle", "Rounded Rectangle", "Ellipse"]
                                            currentIndex: String(clipState.titleShapeType || "") === "rounded-rectangle"
                                                          ? 1
                                                          : String(clipState.titleShapeType || "") === "ellipse" ? 2 : 0
                                            onActivated: root.updateTitle({"shapeType": currentIndex === 1
                                                                                  ? "rounded-rectangle"
                                                                                  : currentIndex === 2 ? "ellipse" : "rectangle"})
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: theme.space4

                                            InspectorSlider {
                                                objectName: "timelineGraphicShapeWidth"
                                                Layout.fillWidth: true
                                                label: "Width"
                                                valueText: Math.round(Number(clipState.titleShapeWidth || 0.55) * 100) + "%"
                                                from: 0.05
                                                to: 1.0
                                                value: Number(clipState.titleShapeWidth || 0.55)
                                                defaultValue: 0.55
                                                valueInputMode: "percentNormalized"
                                                onInteractionStarted: root.beginInspectorEdit()
                                                onInteractionFinished: root.endInspectorEdit()
                                                onValueDragged: function(value) { root.updateTitle({"shapeWidth": value}) }
                                                onResetRequested: root.updateTitle({"shapeWidth": 0.55})
                                            }

                                            InspectorSlider {
                                                objectName: "timelineGraphicShapeHeight"
                                                Layout.fillWidth: true
                                                label: "Height"
                                                valueText: Math.round(Number(clipState.titleShapeHeight || 0.16) * 100) + "%"
                                                from: 0.03
                                                to: 1.0
                                                value: Number(clipState.titleShapeHeight || 0.16)
                                                defaultValue: 0.16
                                                valueInputMode: "percentNormalized"
                                                onInteractionStarted: root.beginInspectorEdit()
                                                onInteractionFinished: root.endInspectorEdit()
                                                onValueDragged: function(value) { root.updateTitle({"shapeHeight": value}) }
                                                onResetRequested: root.updateTitle({"shapeHeight": 0.16})
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: theme.space4

                                            InspectorSlider {
                                                objectName: "timelineGraphicShapeCornerRadius"
                                                Layout.fillWidth: true
                                                label: "Corner radius"
                                                valueText: Math.round(Number(clipState.titleShapeCornerRadius || 0) * 100) + "%"
                                                from: 0.0
                                                to: 0.5
                                                value: Number(clipState.titleShapeCornerRadius || 0)
                                                defaultValue: 0.0
                                                valueInputMode: "percentNormalized"
                                                enabled: String(clipState.titleShapeType || "") === "rounded-rectangle"
                                                onInteractionStarted: root.beginInspectorEdit()
                                                onInteractionFinished: root.endInspectorEdit()
                                                onValueDragged: function(value) { root.updateTitle({"shapeCornerRadius": value}) }
                                                onResetRequested: root.updateTitle({"shapeCornerRadius": 0})
                                            }

                                            InspectorSlider {
                                                objectName: "timelineGraphicShapeStrokeWidth"
                                                Layout.fillWidth: true
                                                label: "Stroke"
                                                valueText: Math.round(Number(clipState.titleShapeStrokeWidth || 0) * 1000) / 10 + "%"
                                                from: 0.0
                                                to: 0.04
                                                value: Number(clipState.titleShapeStrokeWidth || 0)
                                                defaultValue: 0.0
                                                valueInputMode: "percentNormalized"
                                                onInteractionStarted: root.beginInspectorEdit()
                                                onInteractionFinished: root.endInspectorEdit()
                                                onValueDragged: function(value) { root.updateTitle({"shapeStrokeWidth": value}) }
                                                onResetRequested: root.updateTitle({"shapeStrokeWidth": 0})
                                            }
                                        }
                                    }
                                }

                                InspectorSection {
                                    objectName: "timelineTitleMotionSection"
                                    visible: root.isTitleClip && clipState.mediaType === "title"
                                    Layout.fillWidth: true
                                    title: "Motion"
                                    subtitle: "Add editable entrance and exit animation"
                                    expanded: true
                                    showAccentMarker: false

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: theme.space6

                                        Label {
                                            Layout.fillWidth: true
                                            text: root.isGraphicShape ? "Shape animation" : "Title animation"
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeLabel
                                        }

                                        MenuButton {
                                            objectName: "timelineTitleMotionMenuButton"
                                            text: "Choose"
                                            iconName: "motion"
                                            compact: true
                                            enabled: root.isTitleClip && clipState.mediaType === "title"
                                            primaryOpensMenu: true
                                            tooltipText: "Built-ins replace title opacity and position/scale keyframes; Clear removes them."
                                            menuWidth: 220
                                            menuItems: [
                                                {"key": "title-fade", "label": "Fade in and out", "iconName": "visible"},
                                                {"key": "title-pop", "label": "Social pop", "iconName": "motion"},
                                                {"key": "title-rise", "label": "Rise and settle", "iconName": "motion"},
                                                {"key": "title-slide-from-left", "label": "Slide in from left", "iconName": "motion"},
                                                {"key": "title-slide-from-right", "label": "Slide in from right", "iconName": "motion"},
                                                {"key": "title-static", "label": "Clear title motion", "iconName": "reset"}
                                            ]
                                            onMenuItemTriggered: function(key) {
                                                if (root.documentController)
                                                    root.documentController.applySelectedClipMotionPreset(key)
                                            }
                                        }
                                    }
                                }

                                InspectorSection {
                                    objectName: "timelineCaptionEmphasisSection"
                                    visible: root.isTitleClip && clipState.mediaType === "caption"
                                    Layout.fillWidth: true
                                    title: "Word Emphasis"
                                    subtitle: "Uses each editable word's exact In and Out timing"
                                    expanded: true
                                    showAccentMarker: false

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: theme.space6

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: theme.space6

                                            ToneComboBox {
                                                id: captionEmphasisModePicker
                                                objectName: "timelineCaptionEmphasisModePicker"
                                                Layout.fillWidth: true
                                                model: ["None", "Highlight", "Karaoke", "Pop", "Bounce", "Single Word"]
                                                currentIndex: root.captionEmphasisModeIndex()
                                                onActivated: root.setCaptionEmphasis(
                                                                 root.captionEmphasisModeAt(currentIndex),
                                                                 Boolean(root.clipState.captionEmphasisReducedMotion),
                                                                 false)
                                            }

                                            FilterChip {
                                                objectName: "timelineCaptionReducedMotionToggle"
                                                text: "Static Delivery"
                                                active: Boolean(root.clipState.captionEmphasisReducedMotion)
                                                accent: theme.accentWarm400
                                                tooltipText: "Render Pop and Bounce as a static highlight for reduced-motion delivery."
                                                onClicked: root.setCaptionEmphasis(
                                                               String(root.clipState.captionEmphasisMode || "highlight"),
                                                               !Boolean(root.clipState.captionEmphasisReducedMotion),
                                                               false)
                                            }

                                            ActionButton {
                                                objectName: "timelineApplyCaptionEmphasisAllButton"
                                                text: "Apply All"
                                                compact: true
                                                tooltipText: "Use this word-emphasis mode on every caption in one undo step."
                                                onClicked: root.setCaptionEmphasis(
                                                               String(root.clipState.captionEmphasisMode || "highlight"),
                                                               Boolean(root.clipState.captionEmphasisReducedMotion),
                                                               true)
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            visible: (String(root.clipState.captionEmphasisMode || "") === "pop"
                                                      || String(root.clipState.captionEmphasisMode || "") === "bounce")
                                                     && (theme.reducedMotion
                                                         || Boolean(root.clipState.captionEmphasisReducedMotion))
                                            text: Boolean(root.clipState.captionEmphasisReducedMotion)
                                                  ? "Reduced-motion delivery uses a static Highlight while preserving word timing."
                                                  : "Your accessibility preference makes Program static. Delivery keeps the selected motion."
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                }

                                InspectorSection {
                                    visible: root.isTitleClip && clipState.mediaType === "caption"
                                    Layout.fillWidth: true
                                    title: "Word Timing"
                                    subtitle: String(clipState.captionWords ? clipState.captionWords.length : 0) +
                                              " editable words · seconds within this caption"
                                    expanded: true
                                    showAccentMarker: false

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: theme.space4

                                        Repeater {
                                            model: root.isTitleClip && clipState.mediaType === "caption"
                                                   && clipState.captionWords ? clipState.captionWords : []

                                            delegate: Rectangle {
                                                id: captionWordRow
                                                required property var modelData
                                                required property int index

                                                Layout.fillWidth: true
                                                implicitHeight: captionWordFields.implicitHeight + theme.space8
                                                radius: theme.radius4
                                                color: theme.alpha(theme.glassLift, 0.24)
                                                border.width: 0

                                                function commitWord() {
                                                    if (root.documentController)
                                                        root.documentController.updateSelectedCaptionWord(
                                                            index,
                                                            captionWordText.text,
                                                            Number(captionWordStart.text),
                                                            Number(captionWordEnd.text))
                                                }

                                                RowLayout {
                                                    id: captionWordFields
                                                    anchors.fill: parent
                                                    anchors.margins: theme.space4
                                                    spacing: theme.space4

                                                    Label {
                                                        Layout.preferredWidth: 20
                                                        text: String(index + 1)
                                                        color: theme.textFaint
                                                        font.family: theme.fontMono
                                                        font.pixelSize: theme.typeCaption
                                                        horizontalAlignment: Text.AlignHCenter
                                                    }

                                                    ToneTextField {
                                                        id: captionWordText
                                                        Layout.fillWidth: true
                                                        text: String(modelData.text || "")
                                                        placeholderText: "Word"
                                                        onEditingFinished: captionWordRow.commitWord()
                                                    }

                                                    ToneTextField {
                                                        id: captionWordStart
                                                        Layout.preferredWidth: 54
                                                        text: Number(modelData.startSeconds || 0).toFixed(2)
                                                        placeholderText: "In"
                                                        font.family: theme.fontMono
                                                        onEditingFinished: captionWordRow.commitWord()
                                                    }

                                                    ToneTextField {
                                                        id: captionWordEnd
                                                        Layout.preferredWidth: 54
                                                        text: Number(modelData.endSeconds || 0).toFixed(2)
                                                        placeholderText: "Out"
                                                        font.family: theme.fontMono
                                                        onEditingFinished: captionWordRow.commitWord()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                ToneTextField {
                                    visible: !root.isGraphicShape
                                    Layout.fillWidth: true
                                    text: root.isTitleClip ? String(clipState.titleFontFamily || "Sans Serif") : ""
                                    placeholderText: "Font family"
                                    enabled: root.isTitleClip
                                    onEditingFinished: root.updateTitle({"fontFamily": text})
                                }

                                RowLayout {
                                    visible: !root.isGraphicShape
                                    Layout.fillWidth: true
                                    spacing: theme.space4

                                    InspectorSlider {
                                        Layout.fillWidth: true
                                        label: "Size"
                                        valueText: Math.round(root.isTitleClip
                                                              ? root.animatedClipValue(
                                                                    root.currentClip,
                                                                    "titleFontSize",
                                                                    clipState.titleFontSize || 64)
                                                              : 64) + " px"
                                        from: 12
                                        to: 240
                                        value: root.isTitleClip
                                               ? root.animatedClipValue(
                                                     root.currentClip,
                                                     "titleFontSize",
                                                     clipState.titleFontSize || 64)
                                               : 64
                                        defaultValue: 64
                                        enabled: root.isTitleClip
                                        onInteractionStarted: root.beginInspectorEdit()
                                        onInteractionFinished: root.endInspectorEdit()
                                        onValueDragged: function(value) {
                                            if (root.documentController)
                                                root.documentController.setSelectedTitleFontSize(value)
                                        }
                                        onResetRequested: {
                                            if (root.documentController)
                                                root.documentController.setSelectedTitleFontSize(64)
                                        }
                                    }

                                    ParameterKeyframeButton {
                                        parameterName: "titleFontSize"
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.gapDense

                                    ToneTextField {
                                        Layout.fillWidth: true
                                        text: root.isTitleClip
                                              ? String(root.isGraphicShape
                                                       ? clipState.titleShapeStrokeColor || "#ffffffff"
                                                       : clipState.titleColor || "#ffffffff") : ""
                                        placeholderText: root.isGraphicShape ? "Stroke color" : "Text color"
                                        font.family: theme.fontMono
                                        enabled: root.isTitleClip
                                        visible: !root.isGraphicShape || Number(clipState.titleShapeStrokeWidth || 0) > 0
                                        onEditingFinished: root.updateTitle(root.isGraphicShape
                                                                            ? {"shapeStrokeColor": text}
                                                                            : {"color": text})
                                    }

                                    ToneTextField {
                                        Layout.fillWidth: true
                                        text: root.isTitleClip ? String(clipState.titleBackgroundColor || "#99000000") : ""
                                        placeholderText: root.isGraphicShape ? "Fill color" : "Background"
                                        font.family: theme.fontMono
                                        enabled: root.isTitleClip
                                        onEditingFinished: root.updateTitle({"backgroundColor": text})
                                    }
                                }

                                ToneComboBox {
                                    visible: !root.isGraphicShape
                                    accessibleLabel: qsTr("Title alignment")
                                    Layout.fillWidth: true
                                    model: ["Left", "Center", "Right"]
                                    currentIndex: root.isTitleClip && clipState.titleAlignment === "left" ? 0
                                                  : root.isTitleClip && clipState.titleAlignment === "right" ? 2 : 1
                                    enabled: root.isTitleClip
                                    onActivated: root.updateTitle({"alignment": currentText.toLowerCase()})
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space4

                                    InspectorSlider {
                                        objectName: "timelineTitlePositionXSlider"
                                        Layout.fillWidth: true
                                        label: "Horizontal"
                                        valueText: Math.round(root.titlePositionValue("x") * 100) + "%"
                                        from: 0
                                        to: 1
                                        value: root.titlePositionValue("x")
                                        defaultValue: 0.5
                                        valueInputMode: "percentNormalized"
                                        enabled: root.isTitleClip
                                        onInteractionStarted: root.beginInspectorEdit()
                                        onInteractionFinished: root.endInspectorEdit()
                                        onValueDragged: function(value) { root.setTitlePosition("x", value) }
                                        onResetRequested: root.resetTitlePosition("x")
                                    }

                                    ParameterKeyframeButton {
                                        objectName: "timelineTitlePositionXKeyframeButton"
                                        parameterName: "titlePositionX"
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space4

                                    InspectorSlider {
                                        objectName: "timelineTitlePositionYSlider"
                                        Layout.fillWidth: true
                                        label: "Vertical"
                                        valueText: Math.round(root.titlePositionValue("y") * 100) + "%"
                                        from: 0
                                        to: 1
                                        value: root.titlePositionValue("y")
                                        defaultValue: root.defaultTitlePositionY()
                                        valueInputMode: "percentNormalized"
                                        enabled: root.isTitleClip
                                        onInteractionStarted: root.beginInspectorEdit()
                                        onInteractionFinished: root.endInspectorEdit()
                                        onValueDragged: function(value) { root.setTitlePosition("y", value) }
                                        onResetRequested: root.resetTitlePosition("y")
                                    }

                                    ParameterKeyframeButton {
                                        objectName: "timelineTitlePositionYKeyframeButton"
                                        parameterName: "titlePositionY"
                                    }
                                }

                                InspectorSection {
                                    objectName: "timelineTitleAppearanceSection"
                                    visible: root.isTitleClip
                                    Layout.fillWidth: true
                                    title: "Appearance"
                                    subtitle: "Outline, shadow, and title-card shape"
                                    expanded: false
                                    showAccentMarker: false

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: theme.space4

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: theme.space6

                                            ToneTextField {
                                                objectName: "timelineTitleStrokeColorField"
                                                Layout.fillWidth: true
                                                text: String(clipState.captionStrokeColor || "#ff000000")
                                                placeholderText: "Outline color"
                                                font.family: theme.fontMono
                                                onEditingFinished: root.updateTitle({"strokeColor": text})
                                            }

                                            ToneTextField {
                                                objectName: "timelineTitleShadowColorField"
                                                Layout.fillWidth: true
                                                text: String(clipState.captionShadowColor || "#99000000")
                                                placeholderText: "Shadow color"
                                                font.family: theme.fontMono
                                                onEditingFinished: root.updateTitle({"shadowColor": text})
                                            }
                                        }

                                        InspectorSlider {
                                            objectName: "timelineTitleStrokeWidthSlider"
                                            Layout.fillWidth: true
                                            label: "Outline"
                                            valueText: Number(clipState.captionStrokeWidth || 0).toFixed(1) + " px"
                                            from: 0
                                            to: 12
                                            value: Number(clipState.captionStrokeWidth || 0)
                                            defaultValue: 0
                                            onInteractionStarted: root.beginInspectorEdit()
                                            onInteractionFinished: root.endInspectorEdit()
                                            onValueDragged: function(value) { root.updateTitle({"strokeWidth": value}) }
                                            onResetRequested: root.updateTitle({"strokeWidth": 0})
                                        }

                                        ToneCheckBox {
                                            objectName: "timelineTitleShadowEnabledToggle"
                                            Layout.fillWidth: true
                                            text: "Drop shadow"
                                            checked: Boolean(clipState.captionShadowEnabled)
                                            onToggled: root.updateTitle({"shadowEnabled": checked})
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: theme.space6

                                            InspectorSlider {
                                                Layout.fillWidth: true
                                                label: "Shadow X"
                                                valueText: Number(clipState.captionShadowOffsetX || 0).toFixed(1) + " px"
                                                from: -32
                                                to: 32
                                                value: Number(clipState.captionShadowOffsetX || 0)
                                                defaultValue: 2
                                                onInteractionStarted: root.beginInspectorEdit()
                                                onInteractionFinished: root.endInspectorEdit()
                                                onValueDragged: function(value) { root.updateTitle({"shadowOffsetX": value}) }
                                                onResetRequested: root.updateTitle({"shadowOffsetX": 2})
                                            }

                                            InspectorSlider {
                                                Layout.fillWidth: true
                                                label: "Shadow Y"
                                                valueText: Number(clipState.captionShadowOffsetY || 0).toFixed(1) + " px"
                                                from: -32
                                                to: 32
                                                value: Number(clipState.captionShadowOffsetY || 0)
                                                defaultValue: 3
                                                onInteractionStarted: root.beginInspectorEdit()
                                                onInteractionFinished: root.endInspectorEdit()
                                                onValueDragged: function(value) { root.updateTitle({"shadowOffsetY": value}) }
                                                onResetRequested: root.updateTitle({"shadowOffsetY": 3})
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: theme.space6

                                            InspectorSlider {
                                                Layout.fillWidth: true
                                                label: "Box padding"
                                                valueText: Number(clipState.captionBackgroundPadding || 0).toFixed(0) + " px"
                                                from: 0
                                                to: 64
                                                value: Number(clipState.captionBackgroundPadding || 14)
                                                defaultValue: 14
                                                onInteractionStarted: root.beginInspectorEdit()
                                                onInteractionFinished: root.endInspectorEdit()
                                                onValueDragged: function(value) { root.updateTitle({"backgroundPadding": value}) }
                                                onResetRequested: root.updateTitle({"backgroundPadding": 14})
                                            }

                                            InspectorSlider {
                                                Layout.fillWidth: true
                                                label: "Box radius"
                                                valueText: Number(clipState.captionBackgroundRadius || 0).toFixed(0) + " px"
                                                from: 0
                                                to: 64
                                                value: Number(clipState.captionBackgroundRadius || 8)
                                                defaultValue: 8
                                                onInteractionStarted: root.beginInspectorEdit()
                                                onInteractionFinished: root.endInspectorEdit()
                                                onValueDragged: function(value) { root.updateTitle({"backgroundRadius": value}) }
                                                onResetRequested: root.updateTitle({"backgroundRadius": 8})
                                            }
                                        }
                                    }
                                }

                                ActionButton {
                                    visible: root.isTitleClip && clipState.mediaType === "caption"
                                    Layout.fillWidth: true
                                    text: "Merge With Next"
                                    compact: true
                                    tooltipText: "Join this caption to the next adjacent caption."
                                    onClicked: if (root.documentController) root.documentController.mergeSelectedCaptionWithNext()
                                }
                            }
                        }
                    }
                }
            }

            Loader {
                objectName: "timelineMotionInspectorLoader"
                property bool keepActiveThroughIncubation: false
                active: root.inspectorContentLoadReady
                        && (keepActiveThroughIncubation
                            || (root.inspectorSelectionContextAvailable
                                && (root.activeTab === "motion" || root.motionInspectorLoaded)))
                visible: root.hasClip && root.activeTab === "motion"
                Layout.fillWidth: true
                Layout.fillHeight: visible
                onStatusChanged: {
                    const loading = status === Loader.Loading
                    Qt.callLater(function() { keepActiveThroughIncubation = loading })
                }
                onLoaded: root.motionInspectorLoaded = true

                sourceComponent: Component {
                    Item {

                        Flickable {
                            id: motionFlick
                            objectName: "timelineMotionInspector"
                            anchors.fill: parent
                            contentHeight: motionContent.implicitHeight + theme.panelPaddingCompact * 2
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true
                            contentY: Math.max(0, Math.min(root.contentScrollPosition,
                                                          Math.max(0, contentHeight - height)))
                            onContentYChanged: {
                                if (visible && moving && Math.abs(root.contentScrollPosition - contentY) > 0.5)
                                    root.contentScrollPositionChangeRequested(contentY)
                            }

                            ColumnLayout {
                                id: motionContent
                                x: theme.panelPaddingCompact
                                y: theme.panelPaddingCompact
                                width: parent.width - theme.panelPaddingCompact * 2
                                spacing: theme.gapDense

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space6

                                InspectorSection {
                                    Layout.fillWidth: true
                                    title: "Transform & Crop"
                                }

                                ActionButton {
                                    objectName: "timelineAutoReframeButton"
                                    text: "Motion / Track"
                                    iconName: "fit"
                                    compact: true
                                    enabled: root.hasSelection && ["video", "title", "caption", "photo"].indexOf(String(clipState.mediaType || "")) >= 0
                                    disabledReason: "Select a video, title, caption, or image clip."
                                    tooltipText: "Solve a scene camera, attach motion to a layer, create cutouts, or auto reframe video."
                                    onClicked: root.openAutoReframeRequested()
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: keyframeToolbar.implicitHeight + theme.sectionPaddingCompact * 2
                                radius: 0
                                color: theme.alpha(theme.glassLift, 0.18)
                                border.width: 0

                                ColumnLayout {
                                    id: keyframeToolbar
                                    anchors.fill: parent
                                    anchors.margins: theme.sectionPaddingCompact
                                    spacing: theme.gapDense

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: theme.gapDense

                                        Label {
                                            objectName: "timelineKeyframeToolbarLabel"
                                            text: qsTr("KEYFRAMES")
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: theme.typeCaption
                                            font.weight: Font.DemiBold
                                            Accessible.name: qsTr("Keyframes")
                                            Accessible.description: qsTr("Controls for the selected clip parameter.")
                                        }

                                        Label {
                                            objectName: "timelineKeyframeToolbarCount"
                                            text: String(root.keyframesForParameter(root.selectedKeyframeParameter).length)
                                            color: theme.textStrong
                                            font.family: theme.fontMono
                                            font.pixelSize: theme.typeCaption
                                            Accessible.name: qsTr("%1 keyframes").arg(text)
                                        }

                                        Item { Layout.fillWidth: true }

                                        MenuButton {
                                            text: "More"
                                            compact: true
                                            active: root.keyframeLanesVisible
                                            enabled: root.hasSelection
                                            primaryOpensMenu: true
                                            tooltipText: "Keyframe lanes, batch timing, clipboard, and motion presets."
                                            menuItems: root.keyframeMoreMenuItems()
                                            menuWidth: 240
                                            onMenuItemTriggered: function(key) { root.handleKeyframeMoreAction(key) }
                                        }
                                    }

                                    TimelineKeyframeParameterPicker {
                                        objectName: "timelineInspectorKeyframeParameterPicker"
                                        Layout.fillWidth: true
                                        options: root.keyframeParameterOptions()
                                        currentParameter: root.selectedKeyframeParameter
                                        enabled: root.hasSelection
                                        onParameterSelected: function(parameter) {
                                            root.selectedKeyframeParameter = parameter
                                        }
                                    }

                                    RowLayout {
                                        objectName: "timelineKeyframeToolbarActions"
                                        Layout.fillWidth: true
                                        spacing: theme.space4

                                        ActionButton {
                                            showText: false
                                            compact: true
                                            iconName: "step-back"
                                            enabled: root.hasSelection
                                                     && root.keyframesForParameter(root.selectedKeyframeParameter).length > 0
                                            tooltipText: "Previous keyframe"
                                            onClicked: if (root.documentController)
                                                           root.documentController.seekSelectedClipKeyframe(
                                                               root.selectedKeyframeParameter, -1)
                                        }

                                        ParameterKeyframeButton {
                                            parameterName: root.selectedKeyframeParameter
                                        }

                                        ActionButton {
                                            showText: false
                                            compact: true
                                            iconName: "step-forward"
                                            enabled: root.hasSelection
                                                     && root.keyframesForParameter(root.selectedKeyframeParameter).length > 0
                                            tooltipText: "Next keyframe"
                                            onClicked: if (root.documentController)
                                                           root.documentController.seekSelectedClipKeyframe(
                                                               root.selectedKeyframeParameter, 1)
                                        }

                                        ActionButton {
                                            showText: false
                                            compact: true
                                            iconName: "curve"
                                            active: root.keyframesForParameter(root.selectedKeyframeParameter).length > 0
                                            enabled: root.hasSelection
                                            tooltipText: "Open the curve editor"
                                            onClicked: root.openCurveEditorRequested(root.selectedKeyframeParameter)
                                        }

                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }

                            ToneComboBox {
                                accessibleLabel: qsTr("Keyframe interpolation")
                                Layout.fillWidth: true
                                model: ["Hold", "Linear", "Ease In", "Ease Out", "Ease In Out", "Bezier"]
                                currentIndex: {
                                    const current = root.keyframeAtPlayhead(root.selectedKeyframeParameter)
                                    const label = root.interpolationLabel(current ? String(current.interpolation || "linear") : "linear")
                                    return Math.max(0, model.indexOf(label))
                                }
                                enabled: root.keyframeAtPlayhead(root.selectedKeyframeParameter) !== null
                                onActivated: {
                                    const current = root.keyframeAtPlayhead(root.selectedKeyframeParameter)
                                    if (current && root.documentController)
                                        root.documentController.setSelectedClipKeyframeInterpolation(
                                            root.selectedKeyframeParameter,
                                            Number(current.seconds || 0),
                                            root.interpolationValue(currentText))
                                }
                            }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space8

                                    Label {
                                        text: "Crop mode"
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeLabel
                                    }

                                    ToneComboBox {
                                        objectName: "timelineCropModeControl"
                                        Layout.fillWidth: true
                                        model: ["Fit", "Fill", "Stretch"]
                                        currentIndex: root.hasClip && clipState.frameMode === "fill" ? 1 : root.hasClip && clipState.frameMode === "stretch" ? 2 : 0
                                        enabled: root.hasSelection
                                        onActivated: if (root.documentController) root.documentController.setSelectedFrameMode(currentText.toLowerCase())
                                    }
                                }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "Scale"
                                    valueText: Number(root.hasClip ? (clipState.frameScale || 1) : 1).toFixed(2) + "x"
                                    from: 0.05
                                    to: 4
                                    value: root.hasClip && clipState.frameScale ? clipState.frameScale : 1
                                    defaultValue: 1
                                    enabled: root.hasSelection
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        if (root.documentController)
                                            root.documentController.setSelectedFrameScale(value)
                                    }
                                    onResetRequested: if (root.documentController) root.documentController.setSelectedFrameScale(1)
                                }

                                ParameterKeyframeButton {
                                    parameterName: "frameScale"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "Horizontal"
                                    valueText: Number(root.hasClip ? (clipState.frameOffsetX || 0) : 0).toFixed(2)
                                    from: -4
                                    to: 4
                                    value: root.hasClip && clipState.frameOffsetX ? clipState.frameOffsetX : 0
                                    defaultValue: 0
                                    enabled: root.hasSelection
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        if (root.documentController)
                                            root.documentController.setSelectedFrameOffsetX(value)
                                    }
                                    onResetRequested: if (root.documentController) root.documentController.setSelectedFrameOffsetX(0)
                                }

                                ParameterKeyframeButton {
                                    parameterName: "frameOffsetX"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "Vertical"
                                    valueText: Number(root.hasClip ? (clipState.frameOffsetY || 0) : 0).toFixed(2)
                                    from: -4
                                    to: 4
                                    value: root.hasClip && clipState.frameOffsetY ? clipState.frameOffsetY : 0
                                    defaultValue: 0
                                    enabled: root.hasSelection
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        if (root.documentController)
                                            root.documentController.setSelectedFrameOffsetY(value)
                                    }
                                    onResetRequested: if (root.documentController) root.documentController.setSelectedFrameOffsetY(0)
                                }

                                ParameterKeyframeButton {
                                    parameterName: "frameOffsetY"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "Rotation"
                                    valueText: Number(root.hasClip ? (clipState.frameRotationDegrees || 0) : 0).toFixed(1) + "°"
                                    from: -180
                                    to: 180
                                    value: root.hasClip && clipState.frameRotationDegrees !== undefined
                                           ? clipState.frameRotationDegrees : 0
                                    defaultValue: 0
                                    enabled: root.hasSelection
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        if (root.documentController)
                                            root.documentController.setSelectedFrameRotationDegrees(value)
                                    }
                                    onResetRequested: if (root.documentController) root.documentController.setSelectedFrameRotationDegrees(0)
                                }

                                ParameterKeyframeButton {
                                    parameterName: "frameRotationDegrees"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "Perspective X"
                                    valueText: Math.round(Number(clipState.framePerspectiveX || 0) * 100) + "%"
                                    from: -0.35
                                    to: 0.35
                                    value: Number(clipState.framePerspectiveX || 0)
                                    defaultValue: 0
                                    enabled: root.hasSelection
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        if (root.documentController)
                                            root.documentController.setSelectedFramePerspectiveX(value)
                                    }
                                    onResetRequested: if (root.documentController)
                                                          root.documentController.setSelectedFramePerspectiveX(0)
                                }

                                ParameterKeyframeButton {
                                    parameterName: "framePerspectiveX"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "Perspective Y"
                                    valueText: Math.round(Number(clipState.framePerspectiveY || 0) * 100) + "%"
                                    from: -0.35
                                    to: 0.35
                                    value: Number(clipState.framePerspectiveY || 0)
                                    defaultValue: 0
                                    enabled: root.hasSelection
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        if (root.documentController)
                                            root.documentController.setSelectedFramePerspectiveY(value)
                                    }
                                    onResetRequested: if (root.documentController)
                                                          root.documentController.setSelectedFramePerspectiveY(0)
                                }

                                ParameterKeyframeButton {
                                    parameterName: "framePerspectiveY"
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "Opacity"
                                    valueText: Math.round(Number(root.hasClip && clipState.opacity !== undefined ? clipState.opacity : 1) * 100) + "%"
                                    from: 0
                                    to: 1
                                    value: root.hasClip && clipState.opacity !== undefined ? clipState.opacity : 1
                                    defaultValue: 1
                                    valueInputMode: "percentNormalized"
                                    enabled: root.hasSelection
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        if (root.documentController)
                                            root.documentController.setSelectedClipOpacity(value)
                                    }
                                    onResetRequested: if (root.documentController) root.documentController.setSelectedClipOpacity(1)
                                }

                                ParameterKeyframeButton {
                                    parameterName: "opacity"
                                }
                            }

                            InspectorSection {
                                objectName: "timelineStabilizationSection"
                                Layout.fillWidth: true
                                title: "Stabilization"
                                subtitle: "Smooth handheld motion and correct rolling-shutter skew."
                                expandable: true
                                expanded: false
                                enabled: root.hasSelection && clipState.mediaType === "video"
                                disabledReason: "Stabilization is available for video clips."
                                changed: clipState.stabilizationEnabled === true
                                         || clipState.rollingShutterEnabled === true
                                beforeAfterAvailable: clipState.stabilizationReady === true
                                beforeAfterActive: root.documentController !== null
                                                   && root.documentController.stabilizationCompareBefore === true
                                beforeAfterObjectName: "timelineStabilizationBeforeAfterButton"
                                beforeAfterTooltip: beforeAfterActive
                                                        ? "Show the stabilized Program preview."
                                                        : "Compare with the original clip in Program."
                                resetAvailable: changed
                                                || String(clipState.stabilizationAnalysisState || "not-analyzed")
                                                   !== "not-analyzed"
                                resetObjectName: "timelineResetStabilizationButton"
                                resetTooltip: "Remove stabilization settings and analyzed media."
                                onBeforeAfterRequested: if (root.documentController)
                                                            root.documentController.setStabilizationCompareBefore(
                                                                !root.documentController.stabilizationCompareBefore)
                                onResetRequested: if (root.documentController)
                                                      root.documentController.resetSelectedClipStabilization()

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space6

                                    ToolOptionToggle {
                                        objectName: "timelineStabilizationEnableButton"
                                        Layout.fillWidth: true
                                        label: "Stabilize"
                                        checked: clipState.stabilizationEnabled === true
                                        toggleEnabled: root.hasSelection && clipState.mediaType === "video"
                                                       && (root.documentController === null
                                                           || !root.documentController.stabilizationAnalysisBusy)
                                        helperText: "Estimate camera movement and smooth the shot."
                                        onToggled: function(checked) {
                                            root.setStabilizationSettings(
                                                        checked,
                                                        root.stabilizationSmoothing(),
                                                        root.stabilizationCropProtection(),
                                                        clipState.rollingShutterEnabled === true,
                                                        Number(clipState.rollingShutterAmount || 0))
                                        }
                                    }

                                    ToolOptionToggle {
                                        objectName: "timelineRollingShutterEnableButton"
                                        Layout.fillWidth: true
                                        label: "Rolling shutter"
                                        checked: clipState.rollingShutterEnabled === true
                                        toggleEnabled: root.hasSelection && clipState.mediaType === "video"
                                                       && (root.documentController === null
                                                           || !root.documentController.stabilizationAnalysisBusy)
                                        helperText: "Correct horizontal sensor skew in fast camera moves."
                                        onToggled: function(checked) {
                                            root.setStabilizationSettings(
                                                        clipState.stabilizationEnabled === true,
                                                        root.stabilizationSmoothing(),
                                                        root.stabilizationCropProtection(),
                                                        checked,
                                                        Number(clipState.rollingShutterAmount || 0))
                                        }
                                    }
                                }

                                InspectorSlider {
                                    objectName: "timelineStabilizationSmoothingControl"
                                    Layout.fillWidth: true
                                    label: "Smoothness"
                                    valueText: Math.round(root.stabilizationSmoothing() * 100) + "%"
                                    from: 0
                                    to: 1
                                    value: root.stabilizationSmoothing()
                                    defaultValue: 0.55
                                    valueInputMode: "percentNormalized"
                                    enabled: clipState.stabilizationEnabled === true
                                             && (root.documentController === null
                                                 || !root.documentController.stabilizationAnalysisBusy)
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        root.setStabilizationSettings(
                                                    true, value,
                                                    root.stabilizationCropProtection(),
                                                    clipState.rollingShutterEnabled === true,
                                                    Number(clipState.rollingShutterAmount || 0))
                                    }
                                    onResetRequested: root.setStabilizationSettings(
                                                          true, 0.55,
                                                          root.stabilizationCropProtection(),
                                                          clipState.rollingShutterEnabled === true,
                                                          Number(clipState.rollingShutterAmount || 0))
                                }

                                InspectorSlider {
                                    objectName: "timelineStabilizationCropProtectionControl"
                                    Layout.fillWidth: true
                                    label: "Crop protection"
                                    valueText: Math.round(root.stabilizationCropProtection() * 100) + "%"
                                    from: 0
                                    to: 1
                                    value: root.stabilizationCropProtection()
                                    defaultValue: 0.5
                                    valueInputMode: "percentNormalized"
                                    enabled: clipState.stabilizationEnabled === true
                                             && (root.documentController === null
                                                 || !root.documentController.stabilizationAnalysisBusy)
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        root.setStabilizationSettings(
                                                    true,
                                                    root.stabilizationSmoothing(),
                                                    value,
                                                    clipState.rollingShutterEnabled === true,
                                                    Number(clipState.rollingShutterAmount || 0))
                                    }
                                    onResetRequested: root.setStabilizationSettings(
                                                          true,
                                                          root.stabilizationSmoothing(),
                                                          0.5,
                                                          clipState.rollingShutterEnabled === true,
                                                          Number(clipState.rollingShutterAmount || 0))
                                }

                                Label {
                                    visible: clipState.stabilizationEnabled === true
                                    Layout.fillWidth: true
                                    text: "Lower values preserve framing but can reveal edges; higher values crop farther in."
                                    color: theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeCaption
                                    wrapMode: Text.WordWrap
                                }

                                InspectorSlider {
                                    objectName: "timelineRollingShutterAmountControl"
                                    visible: clipState.rollingShutterEnabled === true
                                    Layout.fillWidth: true
                                    label: "Skew direction"
                                    valueText: {
                                        const amount = Number(clipState.rollingShutterAmount || 0)
                                        return Math.abs(amount) < 0.01 ? "Neutral"
                                             : Math.round(Math.abs(amount) * 100) + (amount < 0 ? "% left" : "% right")
                                    }
                                    from: -1
                                    to: 1
                                    value: Number(clipState.rollingShutterAmount || 0)
                                    defaultValue: 0
                                    enabled: clipState.rollingShutterEnabled === true
                                             && (root.documentController === null
                                                 || !root.documentController.stabilizationAnalysisBusy)
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        root.setStabilizationSettings(
                                                    clipState.stabilizationEnabled === true,
                                                    root.stabilizationSmoothing(),
                                                    root.stabilizationCropProtection(),
                                                    true, value)
                                    }
                                    onResetRequested: root.setStabilizationSettings(
                                                          clipState.stabilizationEnabled === true,
                                                          root.stabilizationSmoothing(),
                                                          root.stabilizationCropProtection(),
                                                          true, 0)
                                }

                                ProgressStrip {
                                    objectName: "timelineStabilizationProgress"
                                    visible: root.documentController !== null
                                             && root.documentController.stabilizationAnalysisBusy
                                    Layout.fillWidth: true
                                    progress: root.documentController !== null
                                              ? Number(root.documentController.stabilizationAnalysisProgress || 0) : 0
                                    accent: theme.accentCool400
                                }

                                Label {
                                    objectName: "timelineStabilizationStatusLabel"
                                    Layout.fillWidth: true
                                    text: {
                                        if (root.documentController !== null
                                                && (root.documentController.stabilizationAnalysisBusy
                                                    || String(root.documentController.stabilizationAnalysisStatus || "").length > 0))
                                            return String(root.documentController.stabilizationAnalysisStatus)
                                        if (clipState.stabilizationReady === true)
                                            return "Ready · responsive Program preview · lossless final export"
                                        if (String(clipState.stabilizationAnalysisState || "") === "needs-build")
                                            return "Settings changed · rebuild the stabilized media"
                                        return "Two-pass analysis builds a lightweight preview and a full-resolution export source."
                                    }
                                    color: clipState.stabilizationReady === true ? theme.success : theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeCaption
                                    wrapMode: Text.WordWrap
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space6

                                    ActionButton {
                                        objectName: "timelineAnalyzeStabilizationButton"
                                        Layout.fillWidth: true
                                        text: String(clipState.stabilizationAnalysisState || "not-analyzed") === "not-analyzed"
                                              ? "Analyze Motion" : "Rebuild Preview"
                                        iconName: "analyze"
                                        compact: true
                                        enabled: root.hasSelection
                                                 && clipState.mediaType === "video"
                                                 && (clipState.stabilizationEnabled === true
                                                     || clipState.rollingShutterEnabled === true)
                                                 && root.documentController !== null
                                                 && !root.documentController.stabilizationAnalysisBusy
                                        disabledReason: "Enable stabilization or rolling-shutter correction first."
                                        tooltipText: "Analyze this clip, then build preview and lossless stabilized media."
                                        onClicked: if (root.documentController)
                                                       root.documentController.analyzeSelectedClipStabilization()
                                    }

                                    ActionButton {
                                        objectName: "timelineCancelStabilizationButton"
                                        visible: root.documentController !== null
                                                 && root.documentController.stabilizationAnalysisBusy
                                        text: "Cancel"
                                        iconName: "close"
                                        tone: "secondary"
                                        compact: true
                                        tooltipText: "Cancel analysis and keep the last valid result."
                                        onClicked: if (root.documentController)
                                                       root.documentController.cancelStabilizationAnalysis()
                                    }
                                }
                            }
                            }
                        }
                    }
                }
            }

            Loader {
                objectName: "timelineAudioInspectorLoader"
                property bool keepActiveThroughIncubation: false
                active: root.inspectorContentLoadReady
                        && (keepActiveThroughIncubation
                            || (root.inspectorSelectionContextAvailable
                                && (root.activeTab === "audio" || root.audioInspectorLoaded)))
                visible: root.hasClip && root.activeTab === "audio"
                Layout.fillWidth: true
                Layout.fillHeight: visible
                onStatusChanged: {
                    const loading = status === Loader.Loading
                    Qt.callLater(function() { keepActiveThroughIncubation = loading })
                }
                onLoaded: root.audioInspectorLoaded = true

                sourceComponent: Component {
                    Item {

                        Flickable {
                            id: audioFlick
                            objectName: "timelineAudioInspector"
                            anchors.fill: parent
                            contentHeight: audioContent.implicitHeight + theme.panelPaddingCompact * 2
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true
                            contentY: Math.max(0, Math.min(root.contentScrollPosition,
                                                          Math.max(0, contentHeight - height)))
                            onContentYChanged: {
                                if (visible && moving && Math.abs(root.contentScrollPosition - contentY) > 0.5)
                                    root.contentScrollPositionChangeRequested(contentY)
                            }

                            ColumnLayout {
                                id: audioContent
                                x: theme.panelPaddingCompact
                                y: theme.panelPaddingCompact
                                width: parent.width - theme.panelPaddingCompact * 2
                                spacing: theme.gapDense

                            InspectorSection {
                                Layout.fillWidth: true
                                title: "Audio"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    label: "Gain"
                                    valueText: Number(root.hasClip ? (clipState.audioGainDb || 0) : 0).toFixed(1) + " dB"
                                    from: -36
                                    to: 0
                                    value: root.hasClip && clipState.audioGainDb !== undefined ? clipState.audioGainDb : 0
                                    defaultValue: 0
                                    enabled: root.hasSelection
                                    onInteractionStarted: root.beginInspectorEdit()
                                    onInteractionFinished: root.endInspectorEdit()
                                    onValueDragged: function(value) {
                                        if (root.documentController)
                                            root.documentController.setSelectedClipAudioGain(value)
                                    }
                                    onResetRequested: if (root.documentController) root.documentController.setSelectedClipAudioGain(0)
                                }

                                ParameterKeyframeButton {
                                    parameterName: "audioGainDb"
                                }
                            }

                            InspectorSlider {
                                objectName: "timelineAudioPanControl"
                                Layout.fillWidth: true
                                label: "Pan"
                                valueText: {
                                    const value = Number(root.hasClip ? (clipState.trackAudioPan || 0) : 0)
                                    return Math.abs(value) < 0.01 ? "Center"
                                           : Math.round(Math.abs(value) * 100) + (value < 0 ? "% L" : "% R")
                                }
                                from: -1
                                to: 1
                                value: root.hasClip && clipState.trackAudioPan !== undefined
                                       ? clipState.trackAudioPan : 0
                                defaultValue: 0
                                enabled: root.hasSelection && Number(clipState.audioTrackRowIndex || -1) >= 0
                                onInteractionStarted: root.beginInspectorEdit()
                                onInteractionFinished: root.endInspectorEdit()
                                onValueDragged: function(value) {
                                    if (root.documentController)
                                        root.documentController.setTrackAutomationValue(
                                                    Number(clipState.audioTrackRowIndex), "audioPan", value)
                                }
                                onResetRequested: if (root.documentController)
                                                      root.documentController.setTrackAutomationValue(
                                                          Number(clipState.audioTrackRowIndex), "audioPan", 0)
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Fade In"
                                valueText: Number(root.hasClip ? (clipState.fadeInSeconds || 0) : 0).toFixed(2) + "s"
                                from: 0
                                to: root.hasClip && clipState.trimmedDurationSeconds ? clipState.trimmedDurationSeconds : 1
                                value: root.hasClip && clipState.fadeInSeconds !== undefined ? clipState.fadeInSeconds : 0
                                defaultValue: 0
                                enabled: root.hasSelection
                                onInteractionStarted: root.beginInspectorEdit()
                                onInteractionFinished: root.endInspectorEdit()
                                onValueDragged: function(value) {
                                    if (root.documentController)
                                        root.documentController.setSelectedClipFadeInSeconds(value)
                                }
                                onResetRequested: if (root.documentController) root.documentController.setSelectedClipFadeInSeconds(0)
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Fade Out"
                                valueText: Number(root.hasClip ? (clipState.fadeOutSeconds || 0) : 0).toFixed(2) + "s"
                                from: 0
                                to: root.hasClip && clipState.trimmedDurationSeconds ? clipState.trimmedDurationSeconds : 1
                                value: root.hasClip && clipState.fadeOutSeconds !== undefined ? clipState.fadeOutSeconds : 0
                                defaultValue: 0
                                enabled: root.hasSelection
                                onInteractionStarted: root.beginInspectorEdit()
                                onInteractionFinished: root.endInspectorEdit()
                                onValueDragged: function(value) {
                                    if (root.documentController)
                                        root.documentController.setSelectedClipFadeOutSeconds(value)
                                }
                                onResetRequested: if (root.documentController) root.documentController.setSelectedClipFadeOutSeconds(0)
                            }

                            Rectangle {
                                objectName: "timelineAudioNormalizationControls"
                                Layout.fillWidth: true
                                implicitHeight: normalizationContent.implicitHeight + theme.space12 * 2
                                radius: theme.radius4
                                color: theme.alpha(theme.glassLift, 0.30)
                                border.width: 0

                                ColumnLayout {
                                    id: normalizationContent
                                    anchors.fill: parent
                                    anchors.margins: theme.space12
                                    spacing: theme.space6

                                    Label {
                                        text: "LOUDNESS NORMALIZATION"
                                        color: theme.textMuted
                                        font.family: theme.fontMono
                                        font.pixelSize: theme.typeCaption
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: root.documentController
                                              ? String(root.documentController.loudnessAnalysisStatus || "Measure the post-mix program.")
                                              : "Measure the post-mix program."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeCaption
                                        wrapMode: Text.Wrap
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: theme.space6

                                        Item { Layout.fillWidth: true }

                                        ActionButton {
                                            objectName: "timelineMeasureLoudnessInspectorButton"
                                            text: root.documentController && root.documentController.loudnessAnalysisBusy
                                                  ? "Cancel" : "Measure"
                                            iconName: "audio"
                                            compact: true
                                            onClicked: {
                                                if (!root.documentController)
                                                    return
                                                if (root.documentController.loudnessAnalysisBusy)
                                                    root.documentController.cancelTimelineLoudnessAnalysis()
                                                else
                                                    root.documentController.analyzeTimelineLoudness()
                                            }
                                        }

                                        ConfirmActionButton {
                                            objectName: "timelineNormalizeLoudnessInspectorButton"
                                            text: "Normalize"
                                            confirmText: "Apply Gain"
                                            helperText: "Apply the measured peak-safe gain as one undoable edit"
                                            compact: true
                                            fillButtonWidth: false
                                            enabled: root.documentController
                                                     && root.documentController.loudnessAnalysisResult.measured === true
                                                     && !root.documentController.loudnessAnalysisBusy
                                            onConfirmed: if (root.documentController)
                                                             root.documentController.normalizeTimelineLoudness()
                                        }
                                    }
                                }
                            }
                            }
                        }
                    }
                }
            }

            Loader {
                objectName: "timelineClipInfoInspectorLoader"
                property bool keepActiveThroughIncubation: false
                active: root.inspectorContentLoadReady
                        && (keepActiveThroughIncubation
                            || (root.inspectorSelectionContextAvailable
                                && (root.activeTab === "clip" || root.clipInspectorLoaded)))
                visible: root.hasClip && root.activeTab === "clip"
                Layout.fillWidth: true
                Layout.fillHeight: visible
                onStatusChanged: {
                    const loading = status === Loader.Loading
                    Qt.callLater(function() { keepActiveThroughIncubation = loading })
                }
                onLoaded: root.clipInspectorLoaded = true

                sourceComponent: Component {
                    Item {
                        objectName: "timelineClipInfoInspector"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.panelPaddingCompact
                            spacing: theme.gapDense

                            InspectorSection {
                                Layout.fillWidth: true
                                title: "Clip Info"
                            }

                            Label {
                                text: "Trim In  •  " + Number(root.hasClip ? (clipState.trimStartSeconds || 0) : 0).toFixed(2) + "s"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeLabel
                            }

                            Label {
                                text: "Trim Out  •  " + Number(root.hasClip ? (clipState.trimEndSeconds || 0) : 0).toFixed(2) + "s"
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeLabel
                            }

                            Label {
                                text: "Duration  •  " + (root.hasClip && clipState.trimmedDurationText ? clipState.trimmedDurationText : "0:00")
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeLabel
                            }

                            Label {
                                text: "Playback Rate  •  " + Number(root.hasClip ? (clipState.playbackRate || 1) : 1).toFixed(2) + "x"
                                visible: !root.hasClip || clipState.retimeActive !== true
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeLabel
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: retimeRow.implicitHeight + theme.space12
                                radius: theme.radius4
                                color: theme.alpha(theme.glassLift, 0.30)
                                border.width: 0

                                RowLayout {
                                    id: retimeRow
                                    anchors.fill: parent
                                    anchors.margins: theme.space6
                                    spacing: theme.space4

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0

                                        Label {
                                            text: "RETIME"
                                            color: theme.textFaint
                                            font.family: theme.fontMono
                                            font.pixelSize: theme.typeCaption
                                            font.weight: Font.DemiBold
                                        }

                                        Label {
                                            text: clipState.retimeActive === true
                                                  ? String(clipState.retimeDirection || "forward").toUpperCase()
                                                    + "  ·  "
                                                    + Math.abs(Number(clipState.effectivePlaybackRate || 0)).toFixed(2) + "x"
                                                  : Number(root.hasClip ? (clipState.playbackRate || 1) : 1).toFixed(2) + "x constant"
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeLabel
                                        }
                                    }

                                    ActionButton {
                                        showText: false
                                        compact: true
                                        iconName: "curve"
                                        visible: clipState.retimeActive === true
                                        tooltipText: "Edit the source-time curve"
                                        onClicked: root.openCurveEditorRequested("sourceTime")
                                    }

                                    MenuButton {
                                        text: "Speed"
                                        compact: true
                                        primaryOpensMenu: true
                                        enabled: root.hasSelection && clipState.mediaType === "video"
                                        tooltipText: "Constant speed, speed ramps, reverse, and freeze frame"
                                        menuWidth: 240
                                        menuItems: [
                                            {"key": "speed-025", "label": "0.25x Slow", "iconName": "step-back"},
                                            {"key": "speed-050", "label": "0.5x Slow", "iconName": "step-back"},
                                            {"key": "speed-100", "label": "1x Normal", "iconName": "play"},
                                            {"key": "speed-200", "label": "2x Fast", "iconName": "step-forward"},
                                            {"key": "speed-400", "label": "4x Fast", "iconName": "step-forward"},
                                            {"key": "ramp-up", "label": "Ramp 0.5x → 2x", "iconName": "curve"},
                                            {"key": "ramp-down", "label": "Ramp 2x → 0.5x", "iconName": "curve"},
                                            {"key": "reverse", "label": "Reverse", "iconName": "step-back"},
                                            {"key": "freeze-frame", "label": "Freeze at Playhead", "iconName": "pause"},
                                            {"key": "reset", "label": "Reset Retiming", "iconName": "refresh"}
                                        ]
                                        onMenuItemTriggered: function(key) {
                                            if (!root.documentController)
                                                return
                                            if (key === "speed-025") root.documentController.setSelectedClipPlaybackRate(0.25)
                                            else if (key === "speed-050") root.documentController.setSelectedClipPlaybackRate(0.5)
                                            else if (key === "speed-100") root.documentController.setSelectedClipPlaybackRate(1.0)
                                            else if (key === "speed-200") root.documentController.setSelectedClipPlaybackRate(2.0)
                                            else if (key === "speed-400") root.documentController.setSelectedClipPlaybackRate(4.0)
                                            else root.documentController.applySelectedClipRetimePreset(key)
                                        }
                                    }
                                }
                            }

                            Label {
                                visible: clipState.retimeActive === true
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: clipState.retimeAudioMuted === true
                                      ? "Reverse and held-frame sections are silent in preview and export."
                                      : String((clipState.retimePoints || []).length) + " source-time points · forward audio stays pitch-preserved"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                            }

                            InspectorSection {
                                objectName: "timelineSpeedRampOptionsSection"
                                visible: clipState.retimeActive === true
                                Layout.fillWidth: true
                                title: "Speed Ramp Options"
                                subtitle: "Audio pitch, synthesized frames, source limits, and cached preview."
                                expandable: true
                                expanded: false
                                changed: clipState.retimeActive === true

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space8

                                    Label {
                                        text: "Audio"
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeLabel
                                    }

                                    ToneComboBox {
                                        objectName: "timelineRetimePitchStrategyControl"
                                        Layout.fillWidth: true
                                        model: ["Preserve Pitch", "Follow Speed", "Mute"]
                                        currentIndex: String(clipState.retimePitchStrategy || "preserve") === "follow-speed"
                                                      ? 1
                                                      : String(clipState.retimePitchStrategy || "preserve") === "mute" ? 2 : 0
                                        onActivated: if (root.documentController)
                                                         root.documentController.setSelectedClipRetimeOptions(
                                                             currentIndex === 1 ? "follow-speed"
                                                                               : currentIndex === 2 ? "mute" : "preserve",
                                                             String(clipState.retimeInterpolationMode || "frame-sampling"))
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space8

                                    Label {
                                        text: "Frames"
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeLabel
                                    }

                                    ToneComboBox {
                                        objectName: "timelineRetimeInterpolationModeControl"
                                        Layout.fillWidth: true
                                        model: ["Frame Sampling", "Frame Blend", "Optical Flow"]
                                        currentIndex: String(clipState.retimeInterpolationMode || "frame-sampling") === "frame-blend"
                                                      ? 1
                                                      : String(clipState.retimeInterpolationMode || "frame-sampling") === "optical-flow" ? 2 : 0
                                        onActivated: if (root.documentController)
                                                         root.documentController.setSelectedClipRetimeOptions(
                                                             String(clipState.retimePitchStrategy || "preserve"),
                                                             currentIndex === 1 ? "frame-blend"
                                                                               : currentIndex === 2 ? "optical-flow" : "frame-sampling")
                                    }
                                }

                                Label {
                                    objectName: "timelineRetimeSourceLimitStatus"
                                    Layout.fillWidth: true
                                    text: String(clipState.retimeSourceLimitStatus || "Source range available")
                                    color: (clipState.retimeSourceLimitWarnings || []).length > 0
                                           ? theme.warning : theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeCaption
                                    wrapMode: Text.WordWrap
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space6

                                    ActionButton {
                                        objectName: "timelineRetimeCurveButton"
                                        Layout.fillWidth: true
                                        text: "Edit Curve"
                                        iconName: "curve"
                                        compact: true
                                        onClicked: root.openCurveEditorRequested("sourceTime")
                                    }

                                    ActionButton {
                                        objectName: "timelineRetimeAddHoldButton"
                                        text: "Add Hold"
                                        iconName: "pause"
                                        compact: true
                                        tooltipText: "Insert a one-second held frame at the playhead and ripple later clips."
                                        onClicked: if (root.documentController) {
                                                       const local = Math.max(
                                                           0, Number(root.documentController.sequencePlayheadSeconds || 0)
                                                           - Number(clipState.startSeconds || 0))
                                                       root.documentController.addSelectedClipRetimeHold(local, 1.0)
                                                   }
                                    }
                                }

                                ActionButton {
                                    objectName: "timelineRetimeBuildPreviewButton"
                                    Layout.fillWidth: true
                                    text: root.documentController && root.documentController.renderCacheBusy
                                          ? "Cancel Cached Preview" : "Build Cached Preview"
                                    iconName: root.documentController && root.documentController.renderCacheBusy
                                              ? "close" : "render"
                                    compact: true
                                    tooltipText: "Build the existing exact final-preview cache with this speed curve and interpolation mode."
                                    onClicked: {
                                        if (!root.documentController)
                                            return
                                        if (root.documentController.renderCacheBusy)
                                            root.documentController.cancelRenderCacheGeneration()
                                        else
                                            root.documentController.generateRenderCache()
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: root.documentController
                                          ? "Preview cache · " + String(root.documentController.renderCacheStatus || "not built")
                                          : "Preview cache · not built"
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: theme.typeCaption
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: root.documentController
        function onEffectStackPresetsChanged() {
            if (!effectPresetPopup.opened)
                return
            const presets = root.effectPresetItems()
            effectPresetSelector.currentIndex = Math.max(0, presets.length - 1)
            root.selectEffectPreset(effectPresetSelector.currentIndex)
        }
    }

    AppPopup {
        id: quickEffectPicker
        objectName: "timelineQuickEffectPicker"
        parent: Overlay.overlay
        modal: true
        dim: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(620), parent ? parent.width - theme.space24 * 2 : theme.scaled(620))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(560), parent ? parent.height - theme.space24 * 2 : theme.scaled(560))))
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: Qt.callLater(() => {
            quickEffectSearch.forceActiveFocus()
            quickEffectSearch.selectAll()
        })

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space8

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        text: qsTr("Add an effect")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: qsTr("Search the library, then press Enter to add the first available effect.")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                ActionButton {
                    objectName: "timelineQuickEffectPickerCloseButton"
                    showText: false
                    iconName: "close"
                    compact: true
                    tooltipText: qsTr("Close effect browser")
                    onClicked: quickEffectPicker.close()
                }
            }

            ToneTextField {
                id: quickEffectSearch
                objectName: "timelineQuickEffectSearch"
                Layout.fillWidth: true
                text: root.effectSearchText
                placeholderText: qsTr("Search effects by name, category, or use")
                accessibleLabel: qsTr("Search effects to add")
                Accessible.description: qsTr("Press Enter to add the first available result, or Down to review the list.")
                onTextEdited: root.effectSearchText = text
                onAccepted: {
                    if (quickEffectList.count <= 0)
                        return
                    const firstAddableIndex = root.filteredEffectLibraryItems.findIndex(effect =>
                        root.hasSelection
                        && root.isEffectCompatible(effect)
                        && !root.isEffectApplied(String(effect.name || "")))
                    quickEffectList.currentIndex = firstAddableIndex >= 0 ? firstAddableIndex : 0
                    Qt.callLater(() => {
                        const row = quickEffectList.currentItem
                        if (!row)
                            return
                        if (row.actionEnabled) {
                            root.addQuickEffect(row.effectName)
                            quickEffectPicker.close()
                        } else {
                            row.forceActiveFocus()
                        }
                    })
                }
                Keys.onDownPressed: function(event) {
                    if (quickEffectList.count <= 0)
                        return
                    quickEffectList.currentIndex = Math.max(0, quickEffectList.currentIndex)
                    Qt.callLater(() => {
                        if (quickEffectList.currentItem)
                            quickEffectList.currentItem.forceActiveFocus()
                    })
                    event.accepted = true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.gapDense

                MenuButton {
                    objectName: "timelineQuickEffectCategoryMenu"
                    text: root.discoveryCategory
                    iconName: root.discoveryCategory === "Favorites" ? "star"
                              : root.discoveryCategory === "Recent" ? "clock" : "filter"
                    compact: true
                    primaryOpensMenu: true
                    menuItems: root.effectCategoryMenuItems()
                    menuWidth: 220
                    tooltipText: qsTr("Filter effects by category, favorites, or recent use.")
                    onMenuItemTriggered: function(key) {
                        root.discoveryCategoryChangeRequested(key)
                    }
                }

                FilterChip {
                    objectName: "timelineQuickEffectCompatibilityFilter"
                    text: qsTr("Compatible")
                    active: root.compatibleOnly
                    Accessible.description: qsTr("Show only effects supported by the selected media type.")
                    onClicked: root.compatibleOnlyChangeRequested(!root.compatibleOnly)
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: qsTr("%1 results").arg(root.filteredEffectLibraryItems.length)
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeCaption
                }
            }

            Label {
                visible: root.filteredEffectLibraryItems.length === 0
                Layout.fillWidth: true
                text: root.discoveryCategory === "Favorites"
                      ? qsTr("No favorite effects yet. Star an effect in the library to keep it here.")
                      : qsTr("No effects match this search and filter.")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.Wrap
            }

            ListView {
                id: quickEffectList
                objectName: "timelineQuickEffectList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: theme.scaled(120)
                clip: true
                reuseItems: true
                cacheBuffer: Math.max(height, theme.scaled(240))
                spacing: theme.gapDense
                model: root.filteredEffectLibraryItems
                keyNavigationEnabled: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: TimelineEffectLibraryRow {
                    previewSource: root.effectPreviewSource
                    favorite: root.isEffectFavorite(effectName)
                    compatible: root.isEffectCompatible(modelData)
                    recent: root.effectNameInList(effectName, root.recentEffectNames)
                    actionEnabled: root.hasSelection && compatible
                                   && !root.isEffectApplied(effectName)
                    actionLabel: root.isEffectApplied(effectName) ? qsTr("Added") : qsTr("Add")
                    onAddRequested: function(effectName) {
                        root.addQuickEffect(effectName)
                        quickEffectPicker.close()
                    }
                    onFavoriteToggleRequested: function(effectName) {
                        root.toggleEffectFavoriteRequested(effectName)
                    }
                }
            }
        }
    }

    AppPopup {
        id: effectPresetPopup
        objectName: "timelineEffectStackPresetPopup"
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(600), parent ? parent.width - theme.space24 * 2 : theme.scaled(600))))
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? Math.max(theme.space24, (parent.height - implicitHeight) / 2) : 0
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape
        onOpened: {
            const presets = root.effectPresetItems()
            effectPresetSelector.currentIndex = Math.max(0, presets.length - 1)
            root.selectEffectPreset(presets.length - 1)
            Qt.callLater(effectPresetNameField.forceActiveFocus)
        }

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space10

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        text: "Effect Stack Presets"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Choose from 12 editable Looksy looks or your saved stacks, then review the exact parameters before applying."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                    }
                }

                ActionButton {
                    iconName: "close"
                    showText: false
                    compact: true
                    tooltipText: "Close effect presets"
                    onClicked: effectPresetPopup.close()
                }
            }

            Label {
                objectName: "timelineEffectPresetMigrationReport"
                visible: root.documentController
                         && String(root.documentController.effectStackPresetMigrationReport || "").length > 0
                Layout.fillWidth: true
                text: root.documentController
                      ? String(root.documentController.effectStackPresetMigrationReport || "") : ""
                color: theme.warning
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8

                ToneTextField {
                    id: effectPresetNameField
                    objectName: "timelineEffectPresetNameField"
                    Layout.fillWidth: true
                    text: root.effectPresetNameDraft
                    placeholderText: root.currentEffectStack().length > 0
                                     ? "Name this stack or save a new revision" : "Add effects before saving"
                    selectByMouse: true
                    enabled: root.currentEffectStack().length > 0
                    onTextChanged: root.effectPresetNameDraft = text
                    onAccepted: saveEffectPresetButton.clicked()
                }

                ActionButton {
                    id: saveEffectPresetButton
                    objectName: "timelineSaveEffectStackPreset"
                    text: "Save Version"
                    iconName: "save"
                    enabled: root.currentEffectStack().length > 0
                             && root.effectPresetNameDraft.trim().length > 0
                    disabledReason: root.currentEffectStack().length === 0
                                    ? "Apply at least one effect before saving a preset."
                                    : "Enter a preset name."
                    tooltipText: "A matching name creates the next retained revision."
                    onClicked: {
                        if (root.documentController
                                && root.documentController.saveSelectedClipEffectStackPreset(
                                    root.effectPresetNameDraft))
                            root.effectPresetNameDraft = ""
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: presetPreviewLayout.implicitHeight + theme.space16
                radius: theme.radiusSurface
                color: theme.alpha(theme.ink850, 0.58)
                border.width: 0

                ColumnLayout {
                    id: presetPreviewLayout
                    anchors.fill: parent
                    anchors.margins: theme.space8
                    spacing: theme.space8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.space8

                        ToneComboBox {
                            id: effectPresetSelector
                            objectName: "timelineEffectPresetVersionSelector"
                            Layout.fillWidth: true
                            model: root.effectPresetLabels()
                            enabled: count > 0
                            displayText: count > 0 && currentIndex >= 0
                                         ? String(model[currentIndex]) : "No saved presets"
                            onActivated: root.selectEffectPreset(currentIndex)
                        }

                        Label {
                            visible: root.selectedEffectPresetPreview.found === true
                            text: String(root.selectedEffectPresetPreview.summary || "")
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeCaption
                        }
                    }

                    Label {
                        visible: String(root.selectedEffectPresetPreview.description || "").length > 0
                        Layout.fillWidth: true
                        text: String(root.selectedEffectPresetPreview.description || "")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                    }

                    Label {
                        visible: root.effectPresetItems().length === 0
                        Layout.fillWidth: true
                        text: "Save the selected clip's current effect stack to reuse it in any timeline."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeBody
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: root.selectedEffectPresetPreview.effects || []

                        delegate: Rectangle {
                            required property var modelData
                            readonly property string presetEffectName: String(modelData.name || "")
                            Layout.fillWidth: true
                            implicitHeight: presetEffectRow.implicitHeight + theme.space8
                            radius: theme.radius4
                            color: theme.alpha(theme.glassLift, 0.30)
                            border.width: 0

                            RowLayout {
                                id: presetEffectRow
                                anchors.fill: parent
                                anchors.margins: theme.space4
                                spacing: theme.space8

                                FilterChip {
                                    objectName: "timelinePresetEffectSelection-" + presetEffectName
                                    Layout.preferredWidth: theme.scaled(104)
                                    text: presetEffectName
                                    active: modelData.available !== false
                                            && root.presetEffectSelection[presetEffectName] === true
                                    enabled: modelData.available !== false
                                    Accessible.description: modelData.available === false
                                                              ? "Effect is unavailable and will be skipped."
                                                              : "Include or exclude this effect when applying the preset."
                                    onClicked: root.togglePresetEffect(presetEffectName)
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.available === false
                                          ? "Unavailable · retained in preset"
                                          : String(modelData.parameterSummary || "")
                                    color: modelData.available === false ? theme.warning : theme.textMain
                                    font.family: theme.fontMono
                                    font.pixelSize: theme.typeCaption
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Label {
                        objectName: "timelineEffectPresetMissingEffects"
                        visible: (root.selectedEffectPresetPreview.missingEffects || []).length > 0
                        Layout.fillWidth: true
                        text: "Unavailable effects will not be substituted: "
                              + (root.selectedEffectPresetPreview.missingEffects || []).join(", ")
                        color: theme.warning
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8

                FilterChip {
                    objectName: "timelineEffectPresetReplaceMode"
                    text: "Replace current stack"
                    active: root.effectPresetReplaceExisting
                    Accessible.description: "When active, remove existing effects before applying selected preset effects."
                    onClicked: root.effectPresetReplaceExisting = !root.effectPresetReplaceExisting
                }

                Item { Layout.fillWidth: true }

                ConfirmActionButton {
                    objectName: "timelineDeleteEffectStackPreset"
                    text: "Delete Version"
                    compact: true
                    fillButtonWidth: false
                    visible: root.selectedEffectPresetPreview.found === true
                             && root.selectedEffectPresetPreview.builtIn !== true
                    helperText: "Delete only this saved preset revision"
                    onConfirmed: if (root.documentController)
                                     root.documentController.deleteEffectStackPreset(
                                         String(root.selectedEffectPresetPreview.presetId || ""))
                }

                ActionButton {
                    objectName: "timelineApplyEffectStackPreset"
                    text: "Apply Selected"
                    tone: "primary"
                    enabled: root.isVideoClip
                             && root.selectedEffectPresetPreview.canApply === true
                             && root.selectedPresetEffectNames().length > 0
                    disabledReason: root.isVideoClip
                                    ? "Choose at least one compatible effect."
                                    : "Effect stack looks apply to video clips."
                    onClicked: {
                        if (root.documentController
                                && root.documentController.applyEffectStackPreset(
                                    String(root.selectedEffectPresetPreview.presetId || ""),
                                    root.selectedPresetEffectNames(),
                                    root.effectPresetReplaceExisting))
                            effectPresetPopup.close()
                    }
                }
            }
        }
    }
}
