pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root
    margins: docked ? 0 : theme.space12

    required property var theme
    required property var timelineDocument
    required property var currentClip
    property bool docked: false
    property Item workspaceHost: null
    property bool inspectorContentLoadReady: false
    property var audioTracks: []
    property var trackMeterState: ({})
    property var masterMeter: ({})
    property real momentaryLufs: -60
    property real shortTermLufs: -60
    property bool reducedMotion: false
    property int processorTrackIndex: -1
    property var dialogueCleanupDraft: ({})
    property bool dialogueCleanupAuditionOriginal: false
    property bool dialogueCleanupAdvancedVisible: false
    readonly property bool compactDockHeader: docked && width < theme.scaled(620)
    readonly property var channelModeKeys: [
        "source", "mono-left", "mono-right", "dual-mono", "stereo", "downmix-stereo"
    ]
    readonly property var channelModeLabels: [
        "Use source", "Left channel as mono", "Right channel as mono",
        "Dual mono", "Stereo pair", "Multichannel → stereo"
    ]
    readonly property string timelineAudioMixerContract: "timeline-audio-mixer-window:v5|strips:clip-track-submix-master|roles:dialogue-music-ambience-effects-voiceover-commentary|role-routing:dialogue-music-sfx|role-batch:mute-solo-gain-route|processors:eq-compressor-limiter-gate-deesser-highpass-lowpass|dialogue-cleanup:voice-isolation-noise-hum-dereverb|cleanup-preview:transient-original-cleaned-ab|cleanup-apply:atomic-undo|cleanup-artifacts:smoothing-ambience|ducking:role-generated-editable-regenerable-manual-preserved|controls:mute-solo-pan-gain-fades-inserts|meters:peak-truepeak-rms-lufs|overload:resettable|surface:modular-progressive"
    readonly property string audioChannelMappingContract: "timeline-audio-channel-window:v1|source-metadata:count-layout|routing:six-modes|phase:left-right|swap:stereo|preview:live-diagram-and-program|reset:source|surface:focused-modal"

    signal resetMetersRequested()
    signal reducedMotionRequested(bool enabled)
    signal syncExternalAudioRequested()
    signal dockCloseRequested()

    function meterForTrack(trackId) {
        return trackMeterState[String(trackId || "")] || ({
            "left": 0, "right": 0, "truePeakLeft": 0, "truePeakRight": 0,
            "rmsLeft": 0, "rmsRight": 0, "overloadLatched": false
        })
    }

    function busMeter(busName) {
        const normalized = String(busName || "main").toLowerCase()
        let leftEnergy = 0
        let rightEnergy = 0
        let peakLeft = 0
        let peakRight = 0
        let truePeakLeft = 0
        let truePeakRight = 0
        let overload = false
        let count = 0
        for (let index = 0; index < audioTracks.length; ++index) {
            const track = audioTracks[index]
            if (String(track.audioBus || "main").toLowerCase() !== normalized)
                continue
            const meter = meterForTrack(track.trackId)
            leftEnergy += Math.pow(Number(meter.rmsLeft || 0), 2)
            rightEnergy += Math.pow(Number(meter.rmsRight || 0), 2)
            peakLeft = Math.max(peakLeft, Number(meter.left || 0))
            peakRight = Math.max(peakRight, Number(meter.right || 0))
            truePeakLeft = Math.max(truePeakLeft, Number(meter.truePeakLeft || 0))
            truePeakRight = Math.max(truePeakRight, Number(meter.truePeakRight || 0))
            overload = overload || meter.overloadLatched === true
            ++count
        }
        return {
            "left": peakLeft, "right": peakRight,
            "truePeakLeft": truePeakLeft, "truePeakRight": truePeakRight,
            "rmsLeft": Math.sqrt(leftEnergy), "rmsRight": Math.sqrt(rightEnergy),
            "overloadLatched": overload, "trackCount": count
        }
    }

    function roleSummary(roleName) {
        return timelineDocument.audioRoleSummary(String(roleName || "dialogue"))
    }

    function processorTrack() {
        for (let index = 0; index < audioTracks.length; ++index) {
            if (Number(audioTracks[index].trackIndex) === processorTrackIndex)
                return audioTracks[index]
        }
        return ({})
    }

    function dialogueCleanupSettings() {
        return {
            "enabled": dialogueCleanupDraft.enabled === true,
            "voiceIsolationAmount": Number(dialogueCleanupDraft.voiceIsolationAmount || 0),
            "noiseReductionAmount": Number(dialogueCleanupDraft.noiseReductionAmount || 0),
            "dereverbAmount": Number(dialogueCleanupDraft.dereverbAmount || 0),
            "humRemovalAmount": Number(dialogueCleanupDraft.humRemovalAmount || 0),
            "humFrequencyHz": Number(dialogueCleanupDraft.humFrequencyHz || 60),
            "artifactSmoothing": Number(dialogueCleanupDraft.artifactSmoothing || 0),
            "ambiencePreservation": Number(dialogueCleanupDraft.ambiencePreservation || 0)
        }
    }

    function updateDialogueCleanupPreview() {
        if (processorTrackIndex < 0)
            return
        timelineDocument.previewTrackDialogueCleanup(
                    processorTrackIndex, dialogueCleanupSettings(),
                    dialogueCleanupAuditionOriginal)
    }

    function setDialogueCleanupDraftValue(key, value) {
        const next = {}
        for (const existingKey in dialogueCleanupDraft)
            next[existingKey] = dialogueCleanupDraft[existingKey]
        next[key] = value
        dialogueCleanupDraft = next
        dialogueCleanupAuditionOriginal = false
        updateDialogueCleanupPreview()
    }

    function openDialogueCleanup(trackIndex) {
        processorTrackIndex = Number(trackIndex)
        const track = processorTrack()
        dialogueCleanupDraft = {
            "enabled": true,
            "voiceIsolationAmount": Number(track.dialogueVoiceIsolationAmount !== undefined
                                             ? track.dialogueVoiceIsolationAmount : 0.65),
            "noiseReductionAmount": Number(track.dialogueNoiseReductionAmount !== undefined
                                             ? track.dialogueNoiseReductionAmount : 0.35),
            "dereverbAmount": Number(track.dialogueDereverbAmount !== undefined
                                      ? track.dialogueDereverbAmount : 0.25),
            "humRemovalAmount": Number(track.dialogueHumRemovalAmount !== undefined
                                        ? track.dialogueHumRemovalAmount : 0.50),
            "humFrequencyHz": Number(track.dialogueHumFrequencyHz !== undefined
                                      ? track.dialogueHumFrequencyHz : 60),
            "artifactSmoothing": Number(track.dialogueArtifactSmoothing !== undefined
                                         ? track.dialogueArtifactSmoothing : 0.65),
            "ambiencePreservation": Number(track.dialogueAmbiencePreservation !== undefined
                                            ? track.dialogueAmbiencePreservation : 0.35)
        }
        dialogueCleanupAuditionOriginal = false
        dialogueCleanupAdvancedVisible = false
        updateDialogueCleanupPreview()
        timelineDialogueCleanupPopup.open()
    }

    function processorDefinitions() {
        return [
            {"label": "EQ · Low", "enabledKey": "audioEqEnabled", "parameter": "audioEqLowDb", "from": -12, "to": 12, "step": 0.5, "defaultValue": 0, "suffix": " dB"},
            {"label": "EQ · Mid", "enabledKey": "audioEqEnabled", "parameter": "audioEqMidDb", "from": -12, "to": 12, "step": 0.5, "defaultValue": 0, "suffix": " dB"},
            {"label": "EQ · High", "enabledKey": "audioEqEnabled", "parameter": "audioEqHighDb", "from": -12, "to": 12, "step": 0.5, "defaultValue": 0, "suffix": " dB"},
            {"label": "Compressor · Threshold", "enabledKey": "audioCompressorEnabled", "parameter": "audioCompressorThresholdDb", "from": -48, "to": -1, "step": 0.5, "defaultValue": -18, "suffix": " dB"},
            {"label": "Compressor · Ratio", "enabledKey": "audioCompressorEnabled", "parameter": "audioCompressorRatio", "from": 1, "to": 20, "step": 0.25, "defaultValue": 4, "suffix": ":1"},
            {"label": "Compressor · Makeup", "enabledKey": "audioCompressorEnabled", "parameter": "audioCompressorMakeupDb", "from": 0, "to": 18, "step": 0.5, "defaultValue": 0, "suffix": " dB"},
            {"label": "Limiter · Ceiling", "enabledKey": "audioLimiterEnabled", "parameter": "audioLimiterCeilingDb", "from": -12, "to": 0, "step": 0.1, "defaultValue": -1, "suffix": " dBTP"},
            {"label": "Gate · Threshold", "enabledKey": "audioGateEnabled", "parameter": "audioGateThresholdDb", "from": -80, "to": -10, "step": 0.5, "defaultValue": -48, "suffix": " dB"},
            {"label": "De-esser · Amount", "enabledKey": "audioDeEsserEnabled", "parameter": "audioDeEsserAmount", "from": 0, "to": 1, "step": 0.01, "defaultValue": 0.35, "suffix": ""},
            {"label": "High-pass", "enabledKey": "audioHighPassEnabled", "parameter": "audioHighPassHz", "from": 20, "to": 1000, "step": 5, "defaultValue": 80, "suffix": " Hz"},
            {"label": "Low-pass", "enabledKey": "audioLowPassEnabled", "parameter": "audioLowPassHz", "from": 1000, "to": 20000, "step": 100, "defaultValue": 16000, "suffix": " Hz"}
        ]
    }

    function channelModeIndex() {
        const index = channelModeKeys.indexOf(String(currentClip.audioChannelMode || "source"))
        return Math.max(0, index)
    }

    function applyChannelMapping(mode, swap, invertLeft, invertRight) {
        timelineDocument.setSelectedClipAudioChannelMapping(
                    String(mode), Boolean(swap), Boolean(invertLeft), Boolean(invertRight))
    }

    function channelPreviewText() {
        const mode = String(currentClip.audioChannelMode || "source")
        let text = mode === "mono-left" ? "L → L + R"
                 : mode === "mono-right" ? "R → L + R"
                 : mode === "dual-mono" ? "CH 1 → L · CH 2 → R"
                 : mode === "downmix-stereo" ? "ALL CHANNELS → L + R"
                 : "L → L · R → R"
        if (currentClip.audioSwapChannels === true)
            text += " · SWAPPED"
        if (currentClip.audioInvertLeftPhase === true)
            text += " · ØL"
        if (currentClip.audioInvertRightPhase === true)
            text += " · ØR"
        return text
    }

    function hasProcessorKeyAtPlayhead(parameter) {
        const points = processorTrack().automationKeyframes || []
        const tolerance = 0.5 / Math.max(1, Number(timelineDocument.framesPerSecond || 30))
        for (let index = 0; index < points.length; ++index) {
            if (String(points[index].parameter) === parameter
                    && Math.abs(Number(points[index].seconds)
                                - Number(timelineDocument.sequencePlayheadSeconds || 0)) <= tolerance)
                return true
        }
        return false
    }

    function processorValue(parameter, fallbackValue) {
        const value = processorTrack()[parameter]
        return value === undefined || value === null ? Number(fallbackValue) : Number(value)
    }

    function triggerHeaderTool(key) {
        if (key === "sync-external") {
            syncExternalAudioRequested()
        } else if (key === "duck-gentle" || key === "duck-standard"
                   || key === "duck-strong" || key === "duck-regenerate") {
            const amount = key === "duck-gentle" ? -6
                         : key === "duck-strong" ? -18 : -12
            timelineDocument.generateDialogueDuckingAutomation(amount, 0.12, 0.35)
        } else if (key === "static-meters") {
            reducedMotionRequested(!reducedMotion)
        } else if (key === "reset-meters") {
            resetMetersRequested()
        }
    }

    parent: docked && workspaceHost ? workspaceHost : Overlay.overlay
    modal: !docked
    focus: !docked
    width: Math.min(popupAvailableWidth, Math.max(1, docked && workspaceHost ? workspaceHost.width
           : Math.min(theme.scaled(1180), parent ? parent.width - theme.space24 * 2 : theme.scaled(1180))))
    height: Math.min(popupAvailableHeight, Math.max(1, docked && workspaceHost ? workspaceHost.height
            : Math.min(theme.scaled(680), parent ? parent.height - theme.space24 * 2 : theme.scaled(680))))
    x: docked ? 0 : parent ? (parent.width - width) / 2 : 0
    y: docked ? 0 : parent ? Math.max(theme.space24, (parent.height - height) / 2) : 0
    padding: docked ? theme.panelPaddingCompact : theme.panelPaddingStandard
    closePolicy: docked ? Popup.NoAutoClose : Popup.CloseOnEscape

    background: PopupSurface {
        radius: root.docked ? 0 : theme.popupRadius
        shadowColor: root.docked ? "transparent" : "#48000000"
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
                    objectName: "timelineStudioAudioMixerTitle"
                    text: "Audio Mixer"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: root.compactDockHeader ? theme.typeSection : theme.typeDialogTitle
                    font.weight: Font.DemiBold
                }

                Label {
                    visible: !root.compactDockHeader
                    text: "Clip, track, submix, and master controls · meters show dBFS, dBTP, RMS, and LUFS"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
            }

            ActionButton {
                objectName: "timelineExternalAudioSyncButton"
                visible: !root.compactDockHeader
                text: "Sync External"
                iconName: "audio"
                compact: true
                enabled: root.timelineDocument.selectedClipCount === 2
                disabledReason: "Select one video clip and one standalone audio clip."
                tooltipText: "Preview timecode, waveform, or manual synchronization before moving external audio."
                onClicked: root.syncExternalAudioRequested()
            }

            MenuButton {
                objectName: "timelineDialogueDuckingMenu"
                visible: !root.compactDockHeader
                text: "Auto Duck"
                iconName: "audio"
                compact: true
                primaryOpensMenu: true
                menuWidth: 250
                tooltipText: "Generate editable gain automation on music, ambience, and effects from dialogue-role clips."
                menuItems: [
                    {"key": "gentle", "label": "Generate Gentle · −6 dB"},
                    {"key": "standard", "label": "Generate Standard · −12 dB"},
                    {"key": "strong", "label": "Generate Strong · −18 dB"},
                    {"key": "regenerate", "label": "Regenerate · Preserve Manual", "separatorBefore": true}
                ]
                onMenuItemTriggered: function(key) {
                    const amount = key === "gentle" ? -6 : key === "strong" ? -18 : -12
                    root.timelineDocument.generateDialogueDuckingAutomation(amount, 0.12, 0.35)
                }
            }

            FilterChip {
                objectName: "timelineAudioMeterReducedMotionToggle"
                visible: !root.compactDockHeader
                text: "Static meters"
                active: root.reducedMotion
                iconName: "pause"
                tooltipText: "Disable meter animation while retaining numeric values and overload detection."
                onClicked: root.reducedMotionRequested(!root.reducedMotion)
            }

            ActionButton {
                objectName: "timelineResetAudioMetersButton"
                visible: !root.compactDockHeader
                text: "Reset Meters"
                iconName: "reset"
                compact: true
                tooltipText: "Clear peak history and every overload latch."
                onClicked: root.resetMetersRequested()
            }

            ActionButton {
                iconName: "close"
                showText: false
                compact: true
                tooltipText: root.docked ? "Return to the Edit workspace"
                                         : "Close mixer"
                onClicked: {
                    root.close()
                    if (root.docked)
                        root.dockCloseRequested()
                }
            }

            MenuButton {
                objectName: "timelineAudioDockToolsMenu"
                visible: root.compactDockHeader
                text: "Tools"
                iconName: "settings"
                compact: true
                primaryOpensMenu: true
                menuWidth: 270
                menuItems: [
                    {"key": "sync-external", "label": "Sync External Audio",
                     "enabled": root.timelineDocument.selectedClipCount === 2},
                    {"key": "duck-gentle", "label": "Auto Duck · Gentle · −6 dB"},
                    {"key": "duck-standard", "label": "Auto Duck · Standard · −12 dB"},
                    {"key": "duck-strong", "label": "Auto Duck · Strong · −18 dB"},
                    {"key": "duck-regenerate", "label": "Regenerate Ducking · Preserve Manual",
                     "separatorBefore": true},
                    {"key": "static-meters", "label": root.reducedMotion
                     ? "Enable Animated Meters" : "Use Static Meters",
                     "separatorBefore": true},
                    {"key": "reset-meters", "label": "Reset Meters"}
                ]
                onMenuItemTriggered: function(key) { root.triggerHeaderTool(key) }
            }
        }

        Flickable {
            objectName: "timelineAudioRoleBatchRail"
            Layout.fillWidth: true
            Layout.preferredHeight: audioRoleBatchRow.implicitHeight
            contentWidth: audioRoleBatchRow.implicitWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Row {
                id: audioRoleBatchRow
                spacing: theme.space6

                Repeater {
                    model: ["Dialogue", "Music", "Ambience", "Effects", "Voiceover", "Commentary"]

                    delegate: MenuButton {
                        required property string modelData
                        readonly property var summary: root.roleSummary(modelData)
                        text: modelData + " · " + String(summary.trackCount || 0)
                        iconName: "audio"
                        compact: true
                        primaryOpensMenu: true
                        enabled: Number(summary.trackCount || 0) > 0
                        disabledReason: "No tracks have this audio role."
                        menuWidth: 210
                        menuItems: [
                            {"key": "mute", "label": summary.allMuted === true ? "Unmute role" : "Mute role"},
                            {"key": "solo", "label": summary.anySolo === true ? "Clear role solo" : "Solo role"},
                            {"key": "gain", "label": "Reset role gain"},
                            {"key": "route", "label": "Route to " + String(summary.defaultBus || "main").toUpperCase()}
                        ]
                        onMenuItemTriggered: function(key) {
                            const role = modelData.toLowerCase()
                            if (key === "mute")
                                root.timelineDocument.setAudioRoleSetting(role, "muted", summary.allMuted !== true)
                            else if (key === "solo")
                                root.timelineDocument.setAudioRoleSetting(role, "solo", summary.anySolo !== true)
                            else if (key === "gain")
                                root.timelineDocument.setAudioRoleSetting(role, "audioGainDb", 0)
                            else if (key === "route")
                                root.timelineDocument.setAudioRoleSetting(role, "audioBus", summary.defaultBus)
                        }
                    }
                }
            }
        }

        ScrollView {
            id: mixerStripsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: root.docked
                           ? Math.max(availableHeight, theme.scaled(620))
                           : availableHeight
            ScrollBar.vertical.policy: root.docked ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            ScrollBar.horizontal.policy: ScrollBar.AsNeeded

            RowLayout {
                height: mixerStripsScroll.contentHeight
                spacing: theme.space8

                Rectangle {
                    objectName: "timelineSelectedClipMixerStrip"
                    visible: root.currentClip
                             && (root.currentClip.hasAudio === true
                                 || root.currentClip.trackKind === "audio")
                    Layout.preferredWidth: theme.scaled(190)
                    Layout.fillHeight: true
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink850, 0.78)
                    border.width: 0

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: theme.space10
                        spacing: theme.space6

                        Label {
                            Layout.fillWidth: true
                            text: "CLIP · " + String(root.currentClip.displayLabel || "Selected")
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeControl
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        StatusChip {
                            text: "PRE-FADER"
                            tone: "neutral"
                            compact: true
                            tooltipText: "Clip gain and fades feed the assigned track strip."
                        }

                        ActionButton {
                            objectName: "timelineAudioChannelMappingButton"
                            Layout.fillWidth: true
                            text: "Channels"
                            iconName: "audio"
                            compact: true
                            tooltipText: "Map mono, stereo, dual-mono, and multichannel source audio before clip processing."
                            onClicked: timelineAudioChannelMappingPopup.open()
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.channelPreviewText()
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeMicro
                            elide: Text.ElideRight
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Clip gain"
                            valueText: Number(root.currentClip.baseAudioGainDb || 0).toFixed(1) + " dB"
                            from: -36
                            to: 0
                            stepSize: 0.5
                            value: Number(root.currentClip.baseAudioGainDb || 0)
                            defaultValue: 0
                            valueInputMode: "raw"
                            onInteractionStarted: root.timelineDocument.beginContinuousEdit()
                            onValueDragged: function(nextValue) {
                                root.timelineDocument.setSelectedClipAudioGain(nextValue)
                            }
                            onInteractionFinished: root.timelineDocument.endContinuousEdit()
                            onResetRequested: root.timelineDocument.setSelectedClipAudioGain(0)
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Fade in"
                            valueText: Number(root.currentClip.fadeInSeconds || 0).toFixed(2) + " s"
                            from: 0
                            to: Math.max(0.1, Number(root.currentClip.trimmedDurationSeconds || 0) / 2)
                            stepSize: 0.01
                            value: Number(root.currentClip.fadeInSeconds || 0)
                            defaultValue: 0
                            valueInputMode: "raw"
                            onValueDragged: function(nextValue) {
                                root.timelineDocument.setSelectedClipFadeInSeconds(nextValue)
                            }
                            onResetRequested: root.timelineDocument.setSelectedClipFadeInSeconds(0)
                        }

                        InspectorSlider {
                            Layout.fillWidth: true
                            label: "Fade out"
                            valueText: Number(root.currentClip.fadeOutSeconds || 0).toFixed(2) + " s"
                            from: 0
                            to: Math.max(0.1, Number(root.currentClip.trimmedDurationSeconds || 0) / 2)
                            stepSize: 0.01
                            value: Number(root.currentClip.fadeOutSeconds || 0)
                            defaultValue: 0
                            valueInputMode: "raw"
                            onValueDragged: function(nextValue) {
                                root.timelineDocument.setSelectedClipFadeOutSeconds(nextValue)
                            }
                            onResetRequested: root.timelineDocument.setSelectedClipFadeOutSeconds(0)
                        }

                        Item { Layout.fillHeight: true }

                        Label {
                            Layout.fillWidth: true
                            text: "Clip automation and fades remain editable on the timeline."
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Repeater {
                    model: root.audioTracks

                    delegate: Rectangle {
                        id: trackStrip
                        required property var modelData
                        Layout.preferredWidth: theme.scaled(202)
                        Layout.fillHeight: true
                        radius: theme.radiusSurface
                        color: theme.alpha(theme.ink850, 0.78)
                        border.width: 0

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.space10
                            spacing: theme.space6

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    text: String(trackStrip.modelData.displayLabel || "Audio")
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeControl
                                    font.weight: Font.DemiBold
                                }

                                StatusChip {
                                    text: String(trackStrip.modelData.audioBus || "main").toUpperCase()
                                    tone: "neutral"
                                    compact: true
                                    minimumReadableWidth: 46
                                    maximumReadableWidth: 72
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                FilterChip {
                                    Layout.fillWidth: true
                                    text: "M"
                                    active: trackStrip.modelData.muted === true
                                    tone: active ? "warning" : "neutral"
                                    tooltipText: "Mute track"
                                    onClicked: root.timelineDocument.toggleTrackMuted(
                                                   Number(trackStrip.modelData.trackIndex || 0))
                                }

                                FilterChip {
                                    Layout.fillWidth: true
                                    text: "S"
                                    active: trackStrip.modelData.solo === true
                                    tone: active ? "accent" : "neutral"
                                    tooltipText: "Solo track"
                                    onClicked: root.timelineDocument.toggleTrackSolo(
                                                   Number(trackStrip.modelData.trackIndex || 0))
                                }

                                MenuButton {
                                    text: "Inserts"
                                    compact: true
                                    primaryOpensMenu: true
                                    menuWidth: 210
                                    menuItems: [
                                        {"key": "dialogue-cleanup", "label": "Dialogue Cleanup…", "icon": "audio"},
                                        {"key": "processors", "label": "Open Processing Stack", "icon": "effects"},
                                        {"key": "compressor", "label": trackStrip.modelData.audioCompressorEnabled === true ? "Bypass Compressor" : "Enable Compressor", "icon": "effects"},
                                        {"key": "ducking", "label": trackStrip.modelData.audioDuckingEnabled === true ? "Bypass Ducking" : "Enable Ducking", "icon": "audio"},
                                        {"key": "eq-reset", "label": "Reset Three-band EQ", "icon": "reset", "separatorBefore": true}
                                    ]
                                    onMenuItemTriggered: function(key) {
                                        const row = Number(trackStrip.modelData.trackIndex || 0)
                                        if (key === "dialogue-cleanup") {
                                            root.openDialogueCleanup(row)
                                        } else if (key === "processors") {
                                            root.processorTrackIndex = row
                                            timelineAudioProcessorPopup.open()
                                        } else if (key === "compressor")
                                            root.timelineDocument.setTrackAudioSetting(
                                                        row, "audioCompressorEnabled",
                                                        trackStrip.modelData.audioCompressorEnabled !== true)
                                        else if (key === "ducking")
                                            root.timelineDocument.setTrackAudioSetting(
                                                        row, "audioDuckingEnabled",
                                                        trackStrip.modelData.audioDuckingEnabled !== true)
                                        else if (key === "eq-reset") {
                                            root.timelineDocument.beginContinuousEdit()
                                            root.timelineDocument.setTrackAudioSetting(row, "audioEqLowDb", 0)
                                            root.timelineDocument.setTrackAudioSetting(row, "audioEqMidDb", 0)
                                            root.timelineDocument.setTrackAudioSetting(row, "audioEqHighDb", 0)
                                            root.timelineDocument.endContinuousEdit()
                                        }
                                    }
                                }
                            }

                            TimelineAudioMeter {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.fillHeight: false
                                Layout.minimumHeight: theme.scaled(180)
                                Layout.preferredHeight: theme.scaled(220)
                                theme: root.theme
                                meter: root.meterForTrack(trackStrip.modelData.trackId)
                                reducedMotion: root.reducedMotion
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Gain"
                                valueText: Number(trackStrip.modelData.audioGainDb || 0).toFixed(1) + " dB"
                                from: -36
                                to: 0
                                stepSize: 0.5
                                value: Number(trackStrip.modelData.audioGainDb || 0)
                                defaultValue: 0
                                valueInputMode: "raw"
                                onInteractionStarted: root.timelineDocument.beginContinuousEdit()
                                onValueDragged: function(nextValue) {
                                    root.timelineDocument.setTrackAutomationValue(
                                                Number(trackStrip.modelData.trackIndex || 0),
                                                "audioGainDb", nextValue)
                                }
                                onInteractionFinished: root.timelineDocument.endContinuousEdit()
                                onResetRequested: root.timelineDocument.setTrackAutomationValue(
                                                      Number(trackStrip.modelData.trackIndex || 0),
                                                      "audioGainDb", 0)
                            }

                            InspectorSlider {
                                Layout.fillWidth: true
                                label: "Pan"
                                valueText: Math.abs(Number(trackStrip.modelData.audioPan || 0)) < 0.005
                                           ? "Center"
                                           : Number(trackStrip.modelData.audioPan || 0) < 0
                                             ? "L " + Math.round(Math.abs(Number(trackStrip.modelData.audioPan)) * 100)
                                             : "R " + Math.round(Number(trackStrip.modelData.audioPan) * 100)
                                from: -1
                                to: 1
                                stepSize: 0.01
                                value: Number(trackStrip.modelData.audioPan || 0)
                                defaultValue: 0
                                valueInputMode: "raw"
                                onInteractionStarted: root.timelineDocument.beginContinuousEdit()
                                onValueDragged: function(nextValue) {
                                    root.timelineDocument.setTrackAutomationValue(
                                                Number(trackStrip.modelData.trackIndex || 0),
                                                "audioPan", nextValue)
                                }
                                onInteractionFinished: root.timelineDocument.endContinuousEdit()
                                onResetRequested: root.timelineDocument.setTrackAutomationValue(
                                                      Number(trackStrip.modelData.trackIndex || 0),
                                                      "audioPan", 0)
                            }

                            ToneComboBox {
                                objectName: "timelineTrackAudioRoleCombo"
                                Layout.fillWidth: true
                                model: ["Dialogue", "Music", "Ambience", "Effects", "Voiceover", "Commentary"]
                                currentIndex: Math.max(0, model.indexOf(
                                    String(trackStrip.modelData.audioRole || "dialogue")
                                        .charAt(0).toUpperCase()
                                    + String(trackStrip.modelData.audioRole || "dialogue").slice(1)))
                                onActivated: function(index) {
                                    root.timelineDocument.setTrackAudioSetting(
                                                Number(trackStrip.modelData.trackIndex || 0),
                                                "audioRole", model[index])
                                }
                            }

                            ToneComboBox {
                                accessibleLabel: qsTr("Track audio bus")
                                Layout.fillWidth: true
                                model: ["Main", "Dialogue", "Music", "SFX"]
                                currentIndex: Math.max(0, model.indexOf(
                                    String(trackStrip.modelData.audioBus || "main")
                                        .charAt(0).toUpperCase()
                                    + String(trackStrip.modelData.audioBus || "main").slice(1)))
                                onActivated: function(index) {
                                    root.timelineDocument.setTrackAudioSetting(
                                                Number(trackStrip.modelData.trackIndex || 0),
                                                "audioBus", model[index])
                                }
                            }
                        }
                    }
                }

                Repeater {
                    model: ["Dialogue", "Music", "SFX", "Main"]

                    delegate: Rectangle {
                        id: busStrip
                        required property string modelData
                        readonly property var liveMeter: root.busMeter(modelData)
                        Layout.preferredWidth: theme.scaled(152)
                        Layout.fillHeight: true
                        radius: theme.radiusSurface
                        color: theme.alpha(theme.ink800, 0.72)
                        border.width: 0

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.space10
                            spacing: theme.space6

                            Label {
                                Layout.fillWidth: true
                                text: busStrip.modelData + " Submix"
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeControl
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                            }

                            StatusChip {
                                text: String(busStrip.liveMeter.trackCount || 0) + " TRACKS"
                                tone: "neutral"
                                compact: true
                            }

                            TimelineAudioMeter {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.fillHeight: true
                                theme: root.theme
                                meter: busStrip.liveMeter
                                reducedMotion: root.reducedMotion
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "Post-track sum"
                                color: theme.textFaint
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "timelineMasterMixerStrip"
                    Layout.preferredWidth: theme.scaled(180)
                    Layout.fillHeight: true
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink750, 0.78)
                    border.width: 0

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: theme.space10
                        spacing: theme.space6

                        Label {
                            text: "MASTER OUTPUT"
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeControl
                            font.weight: Font.DemiBold
                        }

                        StatusChip {
                            text: root.masterMeter.overloadLatched === true ? "OVERLOAD" : "PROGRAM"
                            tone: root.masterMeter.overloadLatched === true ? "danger" : "neutral"
                            compact: true
                        }

                        TimelineAudioMeter {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillHeight: true
                            theme: root.theme
                            meter: root.masterMeter
                            reducedMotion: root.reducedMotion
                            loudnessText: "M " + root.momentaryLufs.toFixed(1)
                                          + "\nS " + root.shortTermLufs.toFixed(1) + " LUFS"
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: root.timelineDocument.loudnessAnalysisBusy
                                  ? "Analyzing…" : "Measure LUFS"
                            iconName: "audio"
                            compact: true
                            enabled: !root.timelineDocument.loudnessAnalysisBusy
                            onClicked: root.timelineDocument.analyzeTimelineLoudness()
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Normalize"
                            iconName: "check"
                            compact: true
                            enabled: root.timelineDocument.loudnessAnalysisResult.measured === true
                            disabledReason: "Measure the post-mix program first."
                            onClicked: root.timelineDocument.normalizeTimelineLoudness()
                        }
                    }
                }
            }
        }
    }

    AppPopup {
        id: timelineAudioChannelMappingPopup
        objectName: "timelineAudioChannelMappingWindow"
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(520), root.width - theme.space24 * 2)))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(470), root.height - theme.space24 * 2)))
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space10

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        text: "Source Channels · " + String(root.currentClip.displayLabel || "Selected")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: "Routing happens before clip gain, fades, track processing, and export."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                ActionButton {
                    showText: false
                    compact: true
                    iconName: "close"
                    tooltipText: "Close source-channel mapping"
                    onClicked: timelineAudioChannelMappingPopup.close()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: sourceChannelSummary.implicitHeight + theme.space10 * 2
                radius: theme.radiusControl
                color: theme.alpha(theme.ink850, 0.72)
                border.width: 0

                RowLayout {
                    id: sourceChannelSummary
                    anchors.fill: parent
                    anchors.margins: theme.space10
                    spacing: theme.space8

                    StatusChip {
                        text: Number(root.currentClip.sourceAudioChannels || 0) > 0
                              ? Number(root.currentClip.sourceAudioChannels) + " CH"
                              : "CHANNELS PENDING"
                        tone: Number(root.currentClip.sourceAudioChannels || 0) > 0
                              ? "neutral" : "warning"
                        compact: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: String(root.currentClip.sourceAudioChannelLayout || "Source layout will appear after media analysis")
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeCaption
                        elide: Text.ElideRight
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                Label {
                    text: "Interpretation"
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                    font.weight: Font.DemiBold
                }

                ToneComboBox {
                    objectName: "timelineAudioChannelModeCombo"
                    Layout.fillWidth: true
                    model: root.channelModeLabels
                    currentIndex: root.channelModeIndex()
                    onActivated: root.applyChannelMapping(
                                     root.channelModeKeys[currentIndex],
                                     root.currentClip.audioSwapChannels,
                                     root.currentClip.audioInvertLeftPhase,
                                     root.currentClip.audioInvertRightPhase)
                }
            }

            Rectangle {
                objectName: "timelineAudioChannelPreviewCard"
                Layout.fillWidth: true
                Layout.preferredHeight: channelPreviewLayout.implicitHeight + theme.space12 * 2
                radius: theme.radiusSurface
                color: theme.alpha(theme.accentCool400, 0.10)
                border.width: 0

                ColumnLayout {
                    id: channelPreviewLayout
                    anchors.fill: parent
                    anchors.margins: theme.space12
                    spacing: theme.space6

                    Label {
                        Layout.fillWidth: true
                        text: root.channelPreviewText()
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeControl
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Program monitoring updates immediately; delivery uses the same ordered mapping before mix processing."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                FilterChip {
                    objectName: "timelineAudioSwapChannelsToggle"
                    Layout.fillWidth: true
                    text: "Swap L/R"
                    active: root.currentClip.audioSwapChannels === true
                    tooltipText: "Exchange the mapped left and right channels."
                    onClicked: root.applyChannelMapping(
                                   root.currentClip.audioChannelMode, !active,
                                   root.currentClip.audioInvertLeftPhase,
                                   root.currentClip.audioInvertRightPhase)
                }

                FilterChip {
                    objectName: "timelineAudioInvertLeftToggle"
                    Layout.fillWidth: true
                    text: "Invert Ø L"
                    active: root.currentClip.audioInvertLeftPhase === true
                    tooltipText: "Invert left-channel polarity."
                    onClicked: root.applyChannelMapping(
                                   root.currentClip.audioChannelMode,
                                   root.currentClip.audioSwapChannels, !active,
                                   root.currentClip.audioInvertRightPhase)
                }

                FilterChip {
                    objectName: "timelineAudioInvertRightToggle"
                    Layout.fillWidth: true
                    text: "Invert Ø R"
                    active: root.currentClip.audioInvertRightPhase === true
                    tooltipText: "Invert right-channel polarity."
                    onClicked: root.applyChannelMapping(
                                   root.currentClip.audioChannelMode,
                                   root.currentClip.audioSwapChannels,
                                   root.currentClip.audioInvertLeftPhase, !active)
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: "Changes are clip-specific, undoable, and saved with the edit."
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }

                ActionButton {
                    objectName: "timelineAudioChannelResetButton"
                    text: "Use Source"
                    iconName: "reset"
                    compact: true
                    onClicked: root.applyChannelMapping("source", false, false, false)
                }
            }
        }
    }

    AppPopup {
        id: timelineDialogueCleanupPopup
        objectName: "timelineDialogueCleanupWindow"
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(560), root.width - theme.space24 * 2)))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(650), root.height - theme.space24 * 2)))
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape
        onClosed: root.timelineDocument.clearTrackDialogueCleanupPreview(
                      root.processorTrackIndex)

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
                        text: "Dialogue Cleanup"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: String(root.processorTrack().displayLabel || "Audio")
                              + " · live preview, one undoable Apply"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                StatusChip {
                    text: root.dialogueCleanupAuditionOriginal ? "ORIGINAL" : "CLEANED"
                    tone: root.dialogueCleanupAuditionOriginal ? "neutral" : "positive"
                    compact: true
                }

                ActionButton {
                    showText: false
                    compact: true
                    iconName: "close"
                    tooltipText: "Cancel cleanup preview"
                    onClicked: timelineDialogueCleanupPopup.close()
                }
            }

            Rectangle {
                objectName: "timelineDialogueCleanupPreviewBar"
                Layout.fillWidth: true
                Layout.preferredHeight: cleanupPreviewRow.implicitHeight + theme.space8 * 2
                radius: theme.radiusControl
                color: theme.alpha(theme.ink800, 0.72)
                border.width: 0

                RowLayout {
                    id: cleanupPreviewRow
                    anchors.fill: parent
                    anchors.margins: theme.space8
                    spacing: theme.space8

                    Label {
                        Layout.fillWidth: true
                        text: "Compare without changing the timeline"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }

                    FilterChip {
                        objectName: "timelineDialogueCleanupOriginalButton"
                        text: "Original"
                        active: root.dialogueCleanupAuditionOriginal
                        onClicked: {
                            root.dialogueCleanupAuditionOriginal = true
                            root.updateDialogueCleanupPreview()
                        }
                    }

                    FilterChip {
                        objectName: "timelineDialogueCleanupCleanedButton"
                        text: "Cleaned"
                        active: !root.dialogueCleanupAuditionOriginal
                        tone: active ? "accent" : "neutral"
                        onClicked: {
                            root.dialogueCleanupAuditionOriginal = false
                            root.updateDialogueCleanupPreview()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: "Cleanup chain"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeControl
                    font.weight: Font.DemiBold
                }

                FilterChip {
                    objectName: "timelineDialogueCleanupEnabledToggle"
                    text: root.dialogueCleanupDraft.enabled === true ? "Enabled" : "Bypassed"
                    active: root.dialogueCleanupDraft.enabled === true
                    tone: active ? "positive" : "neutral"
                    onClicked: root.setDialogueCleanupDraftValue("enabled", !active)
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: parent.width
                    spacing: theme.space8

                    InspectorSlider {
                        objectName: "timelineDialogueVoiceIsolationControl"
                        Layout.fillWidth: true
                        enabled: root.dialogueCleanupDraft.enabled === true
                        label: "Voice isolation"
                        valueText: Math.round(Number(root.dialogueCleanupDraft.voiceIsolationAmount || 0) * 100) + "%"
                        from: 0
                        to: 1
                        stepSize: 0.01
                        value: Number(root.dialogueCleanupDraft.voiceIsolationAmount || 0)
                        defaultValue: 0.65
                        valueInputMode: "raw"
                        onValueDragged: function(nextValue) {
                            root.setDialogueCleanupDraftValue("voiceIsolationAmount", nextValue)
                        }
                        onResetRequested: root.setDialogueCleanupDraftValue("voiceIsolationAmount", 0.65)
                    }

                    InspectorSlider {
                        objectName: "timelineDialogueNoiseReductionControl"
                        Layout.fillWidth: true
                        enabled: root.dialogueCleanupDraft.enabled === true
                        label: "Noise reduction"
                        valueText: Math.round(Number(root.dialogueCleanupDraft.noiseReductionAmount || 0) * 100) + "%"
                        from: 0
                        to: 1
                        stepSize: 0.01
                        value: Number(root.dialogueCleanupDraft.noiseReductionAmount || 0)
                        defaultValue: 0.35
                        valueInputMode: "raw"
                        onValueDragged: function(nextValue) {
                            root.setDialogueCleanupDraftValue("noiseReductionAmount", nextValue)
                        }
                        onResetRequested: root.setDialogueCleanupDraftValue("noiseReductionAmount", 0.35)
                    }

                    InspectorSlider {
                        objectName: "timelineDialogueDereverbControl"
                        Layout.fillWidth: true
                        enabled: root.dialogueCleanupDraft.enabled === true
                        label: "De-reverb"
                        valueText: Math.round(Number(root.dialogueCleanupDraft.dereverbAmount || 0) * 100) + "%"
                        from: 0
                        to: 1
                        stepSize: 0.01
                        value: Number(root.dialogueCleanupDraft.dereverbAmount || 0)
                        defaultValue: 0.25
                        valueInputMode: "raw"
                        onValueDragged: function(nextValue) {
                            root.setDialogueCleanupDraftValue("dereverbAmount", nextValue)
                        }
                        onResetRequested: root.setDialogueCleanupDraftValue("dereverbAmount", 0.25)
                    }

                    InspectorSlider {
                        objectName: "timelineDialogueHumRemovalControl"
                        Layout.fillWidth: true
                        enabled: root.dialogueCleanupDraft.enabled === true
                        label: "Hum removal"
                        valueText: Math.round(Number(root.dialogueCleanupDraft.humRemovalAmount || 0) * 100) + "%"
                        from: 0
                        to: 1
                        stepSize: 0.01
                        value: Number(root.dialogueCleanupDraft.humRemovalAmount || 0)
                        defaultValue: 0.50
                        valueInputMode: "raw"
                        onValueDragged: function(nextValue) {
                            root.setDialogueCleanupDraftValue("humRemovalAmount", nextValue)
                        }
                        onResetRequested: root.setDialogueCleanupDraftValue("humRemovalAmount", 0.50)
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: "Power-line hum"
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }

                        FilterChip {
                            objectName: "timelineDialogueHum50HzButton"
                            text: "50 Hz"
                            active: Number(root.dialogueCleanupDraft.humFrequencyHz || 60) === 50
                            onClicked: root.setDialogueCleanupDraftValue("humFrequencyHz", 50)
                        }

                        FilterChip {
                            objectName: "timelineDialogueHum60HzButton"
                            text: "60 Hz"
                            active: Number(root.dialogueCleanupDraft.humFrequencyHz || 60) === 60
                            onClicked: root.setDialogueCleanupDraftValue("humFrequencyHz", 60)
                        }
                    }

                    ActionButton {
                        objectName: "timelineDialogueCleanupAdvancedButton"
                        Layout.fillWidth: true
                        text: root.dialogueCleanupAdvancedVisible
                              ? "Hide artifact controls" : "Artifact controls"
                        iconName: root.dialogueCleanupAdvancedVisible ? "chevron-up" : "chevron-down"
                        compact: true
                        onClicked: root.dialogueCleanupAdvancedVisible =
                                   !root.dialogueCleanupAdvancedVisible
                    }

                    InspectorSlider {
                        objectName: "timelineDialogueArtifactSmoothingControl"
                        Layout.fillWidth: true
                        visible: root.dialogueCleanupAdvancedVisible
                        enabled: root.dialogueCleanupDraft.enabled === true
                        label: "Artifact smoothing"
                        valueText: Math.round(Number(root.dialogueCleanupDraft.artifactSmoothing || 0) * 100) + "%"
                        from: 0
                        to: 1
                        stepSize: 0.01
                        value: Number(root.dialogueCleanupDraft.artifactSmoothing || 0)
                        defaultValue: 0.65
                        valueInputMode: "raw"
                        onValueDragged: function(nextValue) {
                            root.setDialogueCleanupDraftValue("artifactSmoothing", nextValue)
                        }
                        onResetRequested: root.setDialogueCleanupDraftValue("artifactSmoothing", 0.65)
                    }

                    InspectorSlider {
                        objectName: "timelineDialogueAmbiencePreservationControl"
                        Layout.fillWidth: true
                        visible: root.dialogueCleanupAdvancedVisible
                        enabled: root.dialogueCleanupDraft.enabled === true
                        label: "Preserve ambience"
                        valueText: Math.round(Number(root.dialogueCleanupDraft.ambiencePreservation || 0) * 100) + "%"
                        from: 0
                        to: 1
                        stepSize: 0.01
                        value: Number(root.dialogueCleanupDraft.ambiencePreservation || 0)
                        defaultValue: 0.35
                        valueInputMode: "raw"
                        onValueDragged: function(nextValue) {
                            root.setDialogueCleanupDraftValue("ambiencePreservation", nextValue)
                        }
                        onResetRequested: root.setDialogueCleanupDraftValue("ambiencePreservation", 0.35)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space8

                Label {
                    Layout.fillWidth: true
                    text: "Cancel discards the preview. Apply creates one history step."
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }

                ActionButton {
                    objectName: "timelineDialogueCleanupCancelButton"
                    text: "Cancel"
                    compact: true
                    onClicked: timelineDialogueCleanupPopup.close()
                }

                ActionButton {
                    objectName: "timelineDialogueCleanupApplyButton"
                    text: "Apply"
                    iconName: "check"
                    compact: true
                    tone: "primary"
                    onClicked: {
                        root.timelineDocument.applyTrackDialogueCleanup(
                                    root.processorTrackIndex,
                                    root.dialogueCleanupSettings())
                        timelineDialogueCleanupPopup.close()
                    }
                }
            }
        }
    }

    AppPopup {
        id: timelineAudioProcessorPopup
        objectName: "timelineAudioProcessorWindow"
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(620), root.width - theme.space24 * 2)))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(640), root.height - theme.space24 * 2)))
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape

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
                        text: "Processing · " + String(root.processorTrack().displayLabel || "Audio")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: "A compact, ordered track chain. Diamonds write automation at the playhead."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                }

                ActionButton {
                    showText: false
                    compact: true
                    iconName: "close"
                    tooltipText: "Close processing stack"
                    onClicked: timelineAudioProcessorPopup.close()
                }
            }

            Rectangle {
                objectName: "timelineAudioMatchedBypassBar"
                Layout.fillWidth: true
                Layout.preferredHeight: matchedBypassRow.implicitHeight + theme.space8 * 2
                radius: theme.radiusControl
                color: theme.alpha(theme.ink800, 0.72)
                border.width: 0

                RowLayout {
                    id: matchedBypassRow
                    anchors.fill: parent
                    anchors.margins: theme.space8
                    spacing: theme.space8

                    StatusChip {
                        text: root.processorTrack().audioProcessingBypassed === true
                              ? "BYPASSED" : "PROCESSING"
                        tone: root.processorTrack().audioProcessingBypassed === true
                              ? "warning" : "positive"
                        compact: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.processorTrack().audioLoudnessMatchedBypass === true
                              ? "Bypass comparison is gain-compensated."
                              : "Bypass comparison uses raw level."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }

                    FilterChip {
                        objectName: "timelineAudioLoudnessMatchedBypassToggle"
                        text: "Match loudness"
                        active: root.processorTrack().audioLoudnessMatchedBypass === true
                        onClicked: root.timelineDocument.setTrackAudioSetting(
                                       root.processorTrackIndex,
                                       "audioLoudnessMatchedBypass", !active)
                    }

                    ActionButton {
                        objectName: "timelineAudioProcessingBypassButton"
                        text: root.processorTrack().audioProcessingBypassed === true
                              ? "Enable chain" : "Bypass chain"
                        iconName: "compare"
                        compact: true
                        onClicked: root.timelineDocument.setTrackAudioSetting(
                                       root.processorTrackIndex,
                                       "audioProcessingBypassed",
                                       root.processorTrack().audioProcessingBypassed !== true)
                    }
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: parent.width
                    spacing: theme.space6

                    Repeater {
                        model: root.processorDefinitions()

                        delegate: Rectangle {
                            id: processorRow
                            required property var modelData
                            readonly property var track: root.processorTrack()
                            Layout.fillWidth: true
                            Layout.preferredHeight: processorControls.implicitHeight + theme.space8 * 2
                            radius: theme.radiusControl
                            color: theme.alpha(theme.ink850, 0.68)
                            border.width: 0

                            RowLayout {
                                id: processorControls
                                anchors.fill: parent
                                anchors.margins: theme.space8
                                spacing: theme.space8

                                FilterChip {
                                    Layout.preferredWidth: theme.scaled(152)
                                    text: String(processorRow.modelData.label)
                                    active: processorRow.track[processorRow.modelData.enabledKey] === true
                                    tone: active ? "accent" : "neutral"
                                    tooltipText: active ? "Disable this processor." : "Enable this processor."
                                    onClicked: root.timelineDocument.setTrackAudioSetting(
                                                   root.processorTrackIndex,
                                                   String(processorRow.modelData.enabledKey),
                                                   !active)
                                }

                                InspectorSlider {
                                    Layout.fillWidth: true
                                    enabled: processorRow.track[processorRow.modelData.enabledKey] === true
                                    label: ""
                                    valueText: root.processorValue(
                                                   processorRow.modelData.parameter,
                                                   processorRow.modelData.defaultValue).toFixed(
                                                      processorRow.modelData.step < 0.1 ? 2 : 1)
                                               + String(processorRow.modelData.suffix)
                                    from: Number(processorRow.modelData.from)
                                    to: Number(processorRow.modelData.to)
                                    stepSize: Number(processorRow.modelData.step)
                                    value: root.processorValue(
                                               processorRow.modelData.parameter,
                                               processorRow.modelData.defaultValue)
                                    defaultValue: Number(processorRow.modelData.defaultValue)
                                    valueInputMode: "raw"
                                    onInteractionStarted: root.timelineDocument.beginContinuousEdit()
                                    onValueDragged: function(nextValue) {
                                        root.timelineDocument.setTrackAutomationValue(
                                                    root.processorTrackIndex,
                                                    String(processorRow.modelData.parameter), nextValue)
                                    }
                                    onInteractionFinished: root.timelineDocument.endContinuousEdit()
                                    onResetRequested: root.timelineDocument.setTrackAutomationValue(
                                                          root.processorTrackIndex,
                                                          String(processorRow.modelData.parameter),
                                                          Number(processorRow.modelData.defaultValue))
                                }

                                ActionButton {
                                    objectName: "timelineAudioProcessorKeyframeButton"
                                    showText: false
                                    compact: true
                                    iconName: root.hasProcessorKeyAtPlayhead(
                                                  String(processorRow.modelData.parameter))
                                              ? "keyframe-filled" : "keyframe"
                                    active: root.hasProcessorKeyAtPlayhead(
                                                String(processorRow.modelData.parameter))
                                    tooltipText: active
                                                 ? "Remove the processor keyframe at the playhead."
                                                 : "Add a processor keyframe at the playhead."
                                    onClicked: root.timelineDocument.toggleTrackAutomationKeyframe(
                                                   root.processorTrackIndex,
                                                   String(processorRow.modelData.parameter))
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
