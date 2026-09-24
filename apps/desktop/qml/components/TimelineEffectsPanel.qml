pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

GlassSurface {
    id: root

    required property var theme
    required property var timelineDocument
    required property var currentClip
    required property bool collapsed
    required property string activeTab
    required property var effectLibraryModel
    required property var transitionLibraryModel
    property var favoriteEffectNames: []
    property var recentEffectNames: []
    property string discoveryCategory: "All"
    property bool compatibleOnly: true
    property string effectPreviewSource: ""
    property var selectedEffectPresetPreview: ({})
    property var presetEffectSelection: ({})
    property string effectPresetNameDraft: ""
    property bool effectPresetReplaceExisting: false
    property real contentScrollPosition: 0
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
    readonly property bool isTitleClip: root.hasClip
                                        && (clipState.mediaType === "title" || clipState.mediaType === "caption")
    readonly property string timelineEffectsPanelStyleContract: "timeline-effects-panel-style:v6|surface:borderless|tabs:primary-three-more-menu|effects:seven-real-search-favorites-recents-categories-compatibility-live-thumbnails-versioned-presets-stack-amount-enable-bypass-reset-remove-clear|motion:collapsed-stabilization-analysis|controls:shared|type:shared|spacing:shared|states:no-selection-focused-empty-disabled-modified|layout:video-inspector-preserved"

    radius: theme.radiusPanel
    accent: theme.accentWarm400
    accentRail: false
    elevated: true
    edgeOpacity: 0

    // The controller keeps an empty clip-shaped value while selection is being
    // cleared.  Gate the inspector on the authoritative selection index so
    // stale display fields cannot leave clip controls visible.
    readonly property bool hasClip: hasSingleSelection
                                    && clipState.displayLabel !== undefined
                                    && String(clipState.displayLabel || "").length > 0

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
            labels.push(String(preset.name || "Preset") + " · r"
                        + String(preset.revision || 1)
                        + (preset.latest === false ? " · Previous" : ""))
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
        showText: false
        compact: true
        iconName: playheadKeyframe ? "keyframe-filled" : "keyframe"
        active: playheadKeyframe !== null
        enabled: root.hasSelection
        tooltipText: playheadKeyframe
                     ? "Remove the " + parameterName + " keyframe at the playhead."
                     : "Add a " + parameterName + " keyframe at the playhead."
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
            {"key": "clip", "label": "Clip details", "iconName": "info", "checked": activeTab === "clip"},
            {"key": "title", "label": "Title and captions", "iconName": "text", "checked": activeTab === "title", "enabled": root.isTitleClip,
             "disabledReason": "Select a title or caption clip to edit text."}
        ]
    }

    function handleInspectorMoreAction(key) {
        tabChangeRequested(key)
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
        spacing: theme.gapRelated

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
            spacing: theme.space10

            ActionButton {
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
            spacing: theme.space10

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
                        text: "Select a clip"
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
                    active: activeTab === "transitions" || activeTab === "clip" || activeTab === "title"
                    primaryOpensMenu: true
                    tooltipText: "Transitions, clip details, and title controls."
                    menuItems: root.inspectorMoreMenuItems()
                    menuWidth: 210
                    onMenuItemTriggered: function(key) { root.handleInspectorMoreAction(key) }
                }
            }

            Item {
                objectName: "timelineEffectsInspector"
                visible: root.hasClip && activeTab === "effects"
                Layout.fillWidth: true
                Layout.fillHeight: visible

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.panelPaddingCompact
                    spacing: theme.gapDense

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.gapDense

                        Label {
                            Layout.fillWidth: true
                            text: "APPLIED EFFECTS"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeLabel
                            font.weight: Font.DemiBold
                        }

                        Label {
                            text: root.currentEffectStack().length + " stacked"
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeCaption
                        }

                        ActionButton {
                            objectName: "timelineEffectStackPresetsButton"
                            iconName: "save"
                            showText: false
                            compact: true
                            tooltipText: "Save, preview, version, or selectively apply an effect stack."
                            onClicked: root.openEffectPresetManager()
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

                    Rectangle {
                        Layout.fillWidth: true
                        radius: theme.radius4
                        color: theme.alpha(theme.glassLift, root.currentEffectStack().length > 0 ? 0.42 : 0.22)
                        border.width: 0
                        Layout.preferredHeight: Math.min(
                                                    Math.max(96, root.height * 0.36),
                                                    Math.min(240, Math.max(68, stackColumn.implicitHeight + 22)))
                        clip: true

                        Flickable {
                            id: appliedEffectsFlick
                            objectName: "timelineAppliedEffectsStack"
                            anchors.fill: parent
                            anchors.margins: theme.space10
                            contentHeight: stackColumn.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            ColumnLayout {
                                id: stackColumn
                                width: parent.width
                                spacing: theme.gapDense

                                Repeater {
                                    model: root.currentEffectStack()

                                    delegate: Rectangle {
                                        required property var modelData
                                        readonly property string effectName: String(modelData.name || "")
                                        readonly property bool effectEnabled: modelData.enabled !== false
                                        readonly property bool expandedState: root.effectExpanded(effectName)
                                        readonly property int stackIndex: root.effectStackIndex(effectName)
                                        objectName: "timelineAppliedEffect-" + effectName
                                        Layout.fillWidth: true
                                        implicitHeight: effectControls.implicitHeight + theme.space16
                                        radius: theme.radius4
                                        color: theme.alpha(theme.ink850, effectEnabled ? 0.72 : 0.36)
                                        border.width: 0

                                        ColumnLayout {
                                            id: effectControls
                                            anchors.fill: parent
                                            anchors.margins: theme.space8
                                            spacing: theme.space6

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
                        }
                    }

                    ConfirmActionButton {
                        visible: root.currentEffectStack().length > 0
                        Layout.alignment: Qt.AlignRight
                        fillButtonWidth: false
                        text: "Clear Stack"
                        confirmText: "Clear all effects"
                        compact: true
                        helperText: "Press again to remove every effect from this clip"
                        onConfirmed: if (root.documentController)
                                         root.documentController.clearSelectedClipEffects()
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

            Shortcut {
                sequences: [StandardKey.Find]
                enabled: root.visible && !root.collapsed && root.activeTab === "effects"
                onActivated: {
                    effectSearchField.forceActiveFocus()
                    effectSearchField.selectAll()
                }
            }

            Item {
                objectName: "timelineTransitionsInspector"
                visible: root.hasClip && activeTab === "transitions"
                Layout.fillWidth: true
                Layout.fillHeight: visible

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.panelPaddingCompact
                    spacing: theme.gapDense

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Transitions"
                        showAccentMarker: true
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

            Item {
                objectName: root.hasClip && clipState.mediaType === "caption"
                            ? "timelineCaptionInspector" : "timelineTitleInspector"
                visible: root.hasClip && activeTab === "title"
                Layout.fillWidth: true
                Layout.fillHeight: visible

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
                            showAccentMarker: true
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
                                text: root.activeTitleTemplateLabel()
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
                            Layout.fillWidth: true
                            Layout.preferredHeight: 74
                            text: root.isTitleClip ? String(clipState.titleText || "") : ""
                            placeholderText: "Title text"
                            enabled: root.isTitleClip
                            onActiveFocusChanged: if (!activeFocus) root.updateTitle({"text": text})
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
                            Layout.fillWidth: true
                            text: root.isTitleClip ? String(clipState.titleFontFamily || "Sans Serif") : ""
                            placeholderText: "Font family"
                            enabled: root.isTitleClip
                            onEditingFinished: root.updateTitle({"fontFamily": text})
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Size"
                            valueText: Math.round(Number(root.isTitleClip ? (clipState.titleFontSize || 64) : 64)) + " px"
                            from: 12
                            to: 240
                            value: root.isTitleClip ? Number(clipState.titleFontSize || 64) : 64
                            defaultValue: 64
                            enabled: root.isTitleClip
                            onInteractionStarted: root.beginInspectorEdit()
                            onInteractionFinished: root.endInspectorEdit()
                            onValueDragged: function(value) { root.updateTitle({"fontSize": value}) }
                            onResetRequested: root.updateTitle({"fontSize": 64})
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.gapDense

                            ToneTextField {
                                Layout.fillWidth: true
                                text: root.isTitleClip ? String(clipState.titleColor || "#ffffffff") : ""
                                placeholderText: "Text color"
                                font.family: theme.fontMono
                                enabled: root.isTitleClip
                                onEditingFinished: root.updateTitle({"color": text})
                            }

                            ToneTextField {
                                Layout.fillWidth: true
                                text: root.isTitleClip ? String(clipState.titleBackgroundColor || "#99000000") : ""
                                placeholderText: "Background"
                                font.family: theme.fontMono
                                enabled: root.isTitleClip
                                onEditingFinished: root.updateTitle({"backgroundColor": text})
                            }
                        }

                        ToneComboBox {
                            Layout.fillWidth: true
                            model: ["Left", "Center", "Right"]
                            currentIndex: root.isTitleClip && clipState.titleAlignment === "left" ? 0
                                          : root.isTitleClip && clipState.titleAlignment === "right" ? 2 : 1
                            enabled: root.isTitleClip
                            onActivated: root.updateTitle({"alignment": currentText.toLowerCase()})
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Horizontal"
                            valueText: Math.round(Number(root.isTitleClip && clipState.titlePositionX !== undefined ? clipState.titlePositionX : 0.5) * 100) + "%"
                            from: 0
                            to: 1
                            value: root.isTitleClip && clipState.titlePositionX !== undefined ? Number(clipState.titlePositionX) : 0.5
                            defaultValue: 0.5
                            valueInputMode: "percentNormalized"
                            enabled: root.isTitleClip
                            onInteractionStarted: root.beginInspectorEdit()
                            onInteractionFinished: root.endInspectorEdit()
                            onValueDragged: function(value) { root.updateTitle({"positionX": value}) }
                            onResetRequested: root.updateTitle({"positionX": 0.5})
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Vertical"
                            valueText: Math.round(Number(root.isTitleClip && clipState.titlePositionY !== undefined ? clipState.titlePositionY : 0.82) * 100) + "%"
                            from: 0
                            to: 1
                            value: root.isTitleClip && clipState.titlePositionY !== undefined ? Number(clipState.titlePositionY) : 0.82
                            defaultValue: 0.82
                            valueInputMode: "percentNormalized"
                            enabled: root.isTitleClip
                            onInteractionStarted: root.beginInspectorEdit()
                            onInteractionFinished: root.endInspectorEdit()
                            onValueDragged: function(value) { root.updateTitle({"positionY": value}) }
                            onResetRequested: root.updateTitle({"positionY": 0.82})
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

            Item {
                visible: root.hasClip && activeTab === "motion"
                Layout.fillWidth: true
                Layout.fillHeight: visible

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
                            showAccentMarker: true
                        }

                        ActionButton {
                            objectName: "timelineAutoReframeButton"
                            text: "Auto Reframe"
                            iconName: "fit"
                            compact: true
                            enabled: root.hasSelection && clipState.mediaType === "video"
                            disabledReason: "Select an online video clip to track its subject."
                            tooltipText: "Track the primary subject and create editable Transform keyframes."
                            onClicked: root.openAutoReframeRequested()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: keyframeToolbar.implicitHeight + theme.space12
                        radius: theme.radius4
                        color: theme.alpha(theme.glassLift, 0.32)
                        border.width: 0

                        RowLayout {
                            id: keyframeToolbar
                            anchors.fill: parent
                            anchors.margins: theme.space6
                            spacing: theme.space4

                            Label {
                                Layout.fillWidth: true
                                text: "KEYFRAMES  ·  " + root.keyframesForParameter(root.selectedKeyframeParameter).length
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeCaption
                                font.weight: Font.DemiBold
                            }

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
                    }

                    ToneComboBox {
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
                            from: 1
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
                            label: "Opacity"
                            valueText: Math.round(Number(root.hasClip ? (clipState.opacity || 1) : 1) * 100) + "%"
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

            Item {
                visible: root.hasClip && activeTab === "audio"
                Layout.fillWidth: true
                Layout.fillHeight: visible

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
                        showAccentMarker: true
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

            Item {
                objectName: "timelineClipInfoInspector"
                visible: root.hasClip && activeTab === "clip"
                Layout.fillWidth: true
                Layout.fillHeight: visible

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.panelPaddingCompact
                    spacing: theme.gapDense

                    InspectorSection {
                        Layout.fillWidth: true
                        title: "Clip Info"
                        showAccentMarker: true
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

    Popup {
        id: effectPresetPopup
        objectName: "timelineEffectStackPresetPopup"
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(theme.scaled(600), parent ? parent.width - theme.space24 * 2 : theme.scaled(600))
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? Math.max(theme.space24, (parent.height - implicitHeight) / 2) : 0
        padding: theme.panelPaddingStandard
        closePolicy: Popup.CloseOnEscape
        onOpened: {
            const presets = root.effectPresetItems()
            effectPresetSelector.currentIndex = Math.max(0, presets.length - 1)
            root.selectEffectPreset(presets.length - 1)
            Qt.callLater(effectPresetNameField.forceActiveFocus)
        }

        background: PopupSurface {
            fillColor: theme.alpha(theme.glassBase, 0.98)
            borderColor: "transparent"
        }

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
                        text: "Save revisions, preview exact parameters, and choose which compatible effects to apply."
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
                    helperText: "Delete only this saved preset revision"
                    onConfirmed: if (root.documentController)
                                     root.documentController.deleteEffectStackPreset(
                                         String(root.selectedEffectPresetPreview.presetId || ""))
                }

                ActionButton {
                    objectName: "timelineApplyEffectStackPreset"
                    text: "Apply Selected"
                    tone: "primary"
                    enabled: root.selectedEffectPresetPreview.canApply === true
                             && root.selectedPresetEffectNames().length > 0
                    disabledReason: "Choose at least one compatible effect."
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
