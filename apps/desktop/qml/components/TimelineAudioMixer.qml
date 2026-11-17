pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root
    margins: docked ? 0 : theme.space12

    property bool embeddedDockHeader: false
    required property var theme
    required property var timelineDocument
    required property var timelinePlayback
    required property var voiceoverRecorder
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
    property int selectedAudioTrackIndex: -1
    property var dialogueCleanupDraft: ({})
    property bool dialogueCleanupAuditionOriginal: false
    property bool dialogueCleanupAdvancedVisible: false
    property bool voiceoverPlaybackStartedByPanel: false
    readonly property bool compactHeightDock: docked && height < theme.scaled(520)
    readonly property bool compactWidthDock: docked && width < theme.scaled(560)
    readonly property bool compactDockHeader: docked
            && (width < theme.scaled(920) || compactHeightDock)
    readonly property bool selectedClipAudioAvailable:
        Number(timelineDocument.selectedClipCount || 0) === 1
        && currentClip && String(currentClip.clipId || "").length > 0
        && (currentClip.hasAudio === true || currentClip.trackKind === "audio")
    readonly property var channelModeKeys: [
        "source", "mono-left", "mono-right", "dual-mono", "stereo", "downmix-stereo"
    ]
    readonly property var channelModeLabels: [
        "Use source", "Left channel as mono", "Right channel as mono",
        "Dual mono", "Stereo pair", "Multichannel → stereo"
    ]
    readonly property string timelineAudioMixerContract: "timeline-audio-mixer-window:v9|strips:selected-clip-tracks-submix-master|selection:track-context-and-highlighted-strip|gain:vertical-keyboard-accessible-fader|clip-vs-track:separate-strip|roles:dialogue-music-ambience-effects-voiceover-commentary|role-routing:dialogue-music-sfx|role-batch:mute-solo-gain-route|processors:eq-compressor-limiter-gate-deesser-highpass-lowpass|dialogue-cleanup:voice-isolation-noise-hum-dereverb|cleanup-preview:transient-original-cleaned-ab|cleanup-apply:atomic-undo|cleanup-artifacts:smoothing-ambience|ducking:role-generated-editable-regenerable-manual-preserved|controls:mute-solo-pan-gain-fades-inserts|meters:peak-truepeak-rms-lufs|overload:resettable|compact-height:faders-pan-track-actions-visible-fixed-master|compact-width:header-clip-tools-first-full-track-fixed-master-meter|surface:modular-progressive"
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

    function selectMixerTrack(trackIndex) {
        selectedAudioTrackIndex = Number(trackIndex)
        processorTrackIndex = Number(trackIndex)
    }

    function liveTrackSettings(track) {
        const playhead = Number(timelineDocument.sequencePlayheadSeconds || 0)
        const trackId = String(track && track.trackId || "")
        return trackId.length > 0
                ? timelineDocument.audioTrackPlaybackSettings(trackId, playhead)
                : ({})
    }

    function trackAutomationValue(track, parameter) {
        const settings = liveTrackSettings(track)
        const value = settings[parameter]
        return value === undefined || value === null
                ? Number(track && track[parameter] || 0) : Number(value)
    }

    function compactPanReadout(track) {
        const pan = trackAutomationValue(track, "audioPan")
        return Math.abs(pan) < 0.005 ? "C"
             : (pan < 0 ? "L" : "R") + String(Math.round(Math.abs(pan) * 100))
    }

    function compactPanDescription(track) {
        const pan = trackAutomationValue(track, "audioPan")
        return Math.abs(pan) < 0.005 ? "Centered"
             : (pan < 0 ? "Left " : "Right ") + String(Math.round(Math.abs(pan) * 100))
               + " percent"
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
            {"label": "Limiter · Sample peak", "enabledKey": "audioLimiterEnabled", "parameter": "audioLimiterCeilingDb", "from": -12, "to": 0, "step": 0.1, "defaultValue": -1, "suffix": " dBFS", "scope": "Live preview uses an envelope-smoothed sample-peak limiter. It does not oversample inter-sample peaks or match the FFmpeg delivery limiter."},
            {"label": "Gate · Threshold", "enabledKey": "audioGateEnabled", "parameter": "audioGateThresholdDb", "from": -80, "to": -10, "step": 0.5, "defaultValue": -48, "suffix": " dB"},
            {"label": "De-esser · Dynamic HF", "enabledKey": "audioDeEsserEnabled", "parameter": "audioDeEsserAmount", "from": 0, "to": 1, "step": 0.01, "defaultValue": 0.35, "suffix": "", "scope": "Live preview uses a fixed high-frequency crossover and envelope detector. It is a simplified audition processor and may differ from FFmpeg delivery."},
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
                 : mode === "downmix-stereo" ? "SURROUND → L + R · LFE OMITTED"
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
        const value = liveTrackSettings(processorTrack())[parameter]
        return value === undefined || value === null ? Number(fallbackValue) : Number(value)
    }

    function triggerHeaderTool(key) {
        if (key === "record-voiceover") {
            timelineVoiceoverPanel.open()
        } else if (key === "sync-external") {
            syncExternalAudioRequested()
        } else if (key === "measure-lufs") {
            timelineDocument.analyzeTimelineLoudness()
        } else if (key === "normalize-loudness") {
            timelineDocument.normalizeTimelineLoudness()
        } else if (key === "duck-gentle" || key === "duck-standard"
                   || key === "duck-strong" || key === "duck-regenerate") {
            const amount = key === "duck-gentle" ? -6
                         : key === "duck-strong" ? -18 : -12
            timelineDocument.generateDialogueDuckingAutomation(amount, 0.12, 0.35)
        } else if (key === "static-meters") {
            reducedMotionRequested(!reducedMotion)
        } else if (key === "reset-meters") {
            resetMetersRequested()
        } else if (String(key).indexOf("role-bulk-") === 0) {
            const action = String(key).slice("role-bulk-".length)
            const separator = action.lastIndexOf("-")
            if (separator <= 0)
                return
            const role = action.slice(0, separator)
            const setting = action.slice(separator + 1)
            const summary = roleSummary(role)
            if (setting === "mute")
                timelineDocument.setAudioRoleSetting(role, "muted", summary.allMuted !== true)
            else if (setting === "solo")
                timelineDocument.setAudioRoleSetting(role, "solo", summary.anySolo !== true)
            else if (setting === "gain")
                timelineDocument.setAudioRoleSetting(role, "audioGainDb", 0)
            else if (setting === "route")
                timelineDocument.setAudioRoleSetting(role, "audioBus", summary.defaultBus)
        }
    }

    function compactDockToolsMenuItems() {
        const items = [
            {"key": "record-voiceover", "label": "Record Voiceover…", "icon": "audio"},
            {"key": "sync-external", "label": "Sync External Audio",
             "enabled": timelineDocument.selectedClipCount === 2},
            {"key": "duck-gentle", "label": "Auto Duck · Gentle · −6 dB"},
            {"key": "duck-standard", "label": "Auto Duck · Standard · −12 dB"},
            {"key": "duck-strong", "label": "Auto Duck · Strong · −18 dB"},
            {"key": "duck-regenerate", "label": "Regenerate Ducking · Preserve Manual",
             "separatorBefore": true},
            {"key": "measure-lufs", "label": timelineDocument.loudnessAnalysisBusy
             ? "Measuring Program LUFS…" : "Measure Program LUFS",
             "enabled": !timelineDocument.loudnessAnalysisBusy,
             "separatorBefore": true},
            {"key": "normalize-loudness", "label": "Normalize Program Loudness",
             "enabled": timelineDocument.loudnessAnalysisResult.measured === true},
            {"key": "static-meters", "label": reducedMotion
             ? "Enable Animated Meters" : "Use Static Meters",
             "separatorBefore": true},
            {"key": "reset-meters", "label": "Reset Meters"}
        ]
        const roles = [
            {"key": "dialogue", "label": "Dialogue"},
            {"key": "music", "label": "Music"},
            {"key": "ambience", "label": "Ambience"},
            {"key": "effects", "label": "Effects"},
            {"key": "voiceover", "label": "Voiceover"},
            {"key": "commentary", "label": "Commentary"}
        ]
        for (let index = 0; index < roles.length; ++index) {
            const role = roles[index]
            const summary = roleSummary(role.key)
            const separator = index === 0
            items.push({"key": "role-bulk-" + role.key + "-mute",
                        "label": summary.allMuted === true
                                 ? "Unmute " + role.label : "Mute " + role.label,
                        "enabled": Number(summary.trackCount || 0) > 0,
                        "separatorBefore": separator})
            items.push({"key": "role-bulk-" + role.key + "-solo",
                        "label": summary.anySolo === true
                                 ? "Clear " + role.label + " Solo" : "Solo " + role.label,
                        "enabled": Number(summary.trackCount || 0) > 0})
            items.push({"key": "role-bulk-" + role.key + "-gain",
                        "label": "Reset " + role.label + " Gain",
                        "enabled": Number(summary.trackCount || 0) > 0})
            items.push({"key": "role-bulk-" + role.key + "-route",
                        "label": role.label + " · Route to "
                                 + String(summary.defaultBus || "main").toUpperCase(),
                        "enabled": Number(summary.trackCount || 0) > 0})
        }
        return items
    }

    function trackToolsMenuItems(track) {
        const item = track || ({})
        return [
            {"key": "dialogue-cleanup", "label": "Dialogue Cleanup…", "icon": "audio"},
            {"key": "processors", "label": "Open Processing Stack", "icon": "effects"},
            {"key": "compressor", "label": item.audioCompressorEnabled === true
             ? "Bypass Compressor" : "Enable Compressor", "icon": "effects"},
            {"key": "ducking", "label": item.audioDuckingEnabled === true
             ? "Bypass Ducking" : "Enable Ducking", "icon": "audio"},
            {"key": "eq-reset", "label": "Reset Three-band EQ", "icon": "reset",
             "separatorBefore": true},
            {"key": "pan-center", "label": "Center Pan", "icon": "reset",
             "separatorBefore": true},
            {"key": "role-dialogue", "label": "Role · Dialogue"},
            {"key": "role-music", "label": "Role · Music"},
            {"key": "role-ambience", "label": "Role · Ambience"},
            {"key": "role-effects", "label": "Role · Effects"},
            {"key": "role-voiceover", "label": "Role · Voiceover"},
            {"key": "role-commentary", "label": "Role · Commentary"},
            {"key": "route-main", "label": "Route · Main", "separatorBefore": true},
            {"key": "route-dialogue", "label": "Route · Dialogue"},
            {"key": "route-music", "label": "Route · Music"},
            {"key": "route-sfx", "label": "Route · SFX"}
        ]
    }

    function triggerTrackTool(key, track) {
        const item = track || ({})
        const row = Number(item.trackIndex || 0)
        if (key === "dialogue-cleanup") {
            openDialogueCleanup(row)
        } else if (key === "processors") {
            processorTrackIndex = row
            timelineAudioProcessorPopup.open()
        } else if (key === "compressor") {
            timelineDocument.setTrackAudioSetting(
                row, "audioCompressorEnabled", item.audioCompressorEnabled !== true)
        } else if (key === "ducking") {
            timelineDocument.setTrackAudioSetting(
                row, "audioDuckingEnabled", item.audioDuckingEnabled !== true)
        } else if (key === "eq-reset") {
            timelineDocument.beginContinuousEdit()
            timelineDocument.setTrackAudioSetting(row, "audioEqLowDb", 0)
            timelineDocument.setTrackAudioSetting(row, "audioEqMidDb", 0)
            timelineDocument.setTrackAudioSetting(row, "audioEqHighDb", 0)
            timelineDocument.endContinuousEdit()
        } else if (key === "pan-center") {
            timelineDocument.setTrackAutomationValue(row, "audioPan", 0)
        } else if (String(key).indexOf("role-") === 0) {
            timelineDocument.setTrackAudioSetting(row, "audioRole", String(key).slice(5))
        } else if (String(key).indexOf("route-") === 0) {
            timelineDocument.setTrackAudioSetting(row, "audioBus", String(key).slice(6))
        }
    }

    function audioTrackStripRoles(track) {
        const item = track || ({})
        return {
            "trackId": String(item.trackId || ""),
            "trackIndex": Number(item.trackIndex || 0),
            "displayLabel": String(item.displayLabel || "Audio"),
            "audioBus": String(item.audioBus || "main"),
            "audioRole": String(item.audioRole || "dialogue"),
            "audioGainDb": Number(item.audioGainDb || 0),
            "audioPan": Number(item.audioPan || 0),
            "muted": item.muted === true,
            "solo": item.solo === true,
            "audioCompressorEnabled": item.audioCompressorEnabled === true,
            "audioDuckingEnabled": item.audioDuckingEnabled === true
        }
    }

    function synchronizeAudioTrackRows() {
        const tracks = audioTracks || []
        let matchingRows = stableAudioTrackRows.count === tracks.length
        if (matchingRows) {
            for (let index = 0; index < tracks.length; ++index) {
                const existing = stableAudioTrackRows.get(index)
                if (String(existing.trackId || "") !== String(tracks[index].trackId || "")) {
                    matchingRows = false
                    break
                }
            }
        }

        if (!matchingRows) {
            stableAudioTrackRows.clear()
            for (let index = 0; index < tracks.length; ++index)
                stableAudioTrackRows.append(audioTrackStripRoles(tracks[index]))
        } else {
            // Keep delegate identity while track settings change during a drag.
            // Replacing a JavaScript-array Repeater model on every controller
            // readback would destroy the pressed Slider between pointer moves.
            for (let index = 0; index < tracks.length; ++index) {
                const roles = audioTrackStripRoles(tracks[index])
                for (const role in roles)
                    stableAudioTrackRows.setProperty(index, role, roles[role])
            }
        }
    }

    function startVoiceoverPlayback() {
        voiceoverPlaybackStartedByPanel = timelineDocument.sequencePlaying !== true
        if (voiceoverPlaybackStartedByPanel)
            timelinePlayback.play()
    }

    function stopVoiceoverPlayback() {
        if (voiceoverPlaybackStartedByPanel)
            timelinePlayback.pause()
        voiceoverPlaybackStartedByPanel = false
    }

    function suspendForRelease() {
        stopVoiceoverPlayback()
    }

    Component.onDestruction: stopVoiceoverPlayback()

    ListModel {
        id: stableAudioTrackRows

        Component.onCompleted: root.synchronizeAudioTrackRows()
    }

    onAudioTracksChanged: synchronizeAudioTrackRows()

    parent: docked && workspaceHost ? workspaceHost : Overlay.overlay
    modal: !docked
    focus: !docked
    width: docked && workspaceHost ? Math.max(1, workspaceHost.width)
           : Math.min(popupAvailableWidth, Math.max(1,
               Math.min(theme.scaled(1180), parent ? parent.width - theme.space24 * 2 : theme.scaled(1180))))
    height: docked && workspaceHost ? Math.max(1, workspaceHost.height)
            : Math.min(popupAvailableHeight, Math.max(1,
                Math.min(theme.scaled(680), parent ? parent.height - theme.space24 * 2 : theme.scaled(680))))
    x: docked ? 0 : parent ? (parent.width - width) / 2 : 0
    y: docked ? 0 : parent ? Math.max(theme.space24, (parent.height - height) / 2) : 0
    padding: docked ? theme.panelPaddingCompact : theme.popupPadding
    closePolicy: docked ? Popup.NoAutoClose : Popup.CloseOnEscape

    background: PopupSurface {
        docked: root.docked
        shadowColor: root.docked ? "transparent" : "#48000000"
    }

    contentItem: ColumnLayout {
        spacing: theme.space10

        RowLayout {
            objectName: "timelineAudioMixerHeaderRow"
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

            ActionButton {
                objectName: "timelineRecordVoiceoverButton"
                visible: !root.compactDockHeader
                text: root.voiceoverRecorder.state === "recording" ? "Recording…" : "Voiceover"
                iconName: "audio"
                compact: true
                active: root.voiceoverRecorder.state === "recording"
                         || root.voiceoverRecorder.state === "preroll"
                tooltipText: "Choose an input, set pre-roll, and record a durable WAV take."
                enabled: root.voiceoverRecorder.state !== "permissionPending"
                         && root.voiceoverRecorder.state !== "finalizing"
                onClicked: timelineVoiceoverPanel.open()
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
                visible: !root.embeddedDockHeader
                showText: false
                compact: true
                tooltipText: root.docked ? "Return to the Edit workspace"
                                         : "Close mixer"
                enabled: root.voiceoverRecorder.state !== "permissionPending"
                         && root.voiceoverRecorder.state !== "preroll"
                         && root.voiceoverRecorder.state !== "recording"
                         && root.voiceoverRecorder.state !== "finalizing"
                onClicked: {
                    root.close()
                    if (root.docked)
                        root.dockCloseRequested()
                }
            }

            ActionButton {
                objectName: "timelineCompactSelectedClipToolsButton"
                visible: root.selectedClipAudioAvailable && root.compactWidthDock
                text: "Clip"
                iconName: "audio"
                compact: true
                accessibleName: "Open selected clip audio controls"
                tooltipText: "Open selected clip gain, fades, and source channel mapping."
                onClicked: timelineCompactSelectedClipToolsPopup.open()
            }

            MenuButton {
                objectName: "timelineAudioDockToolsMenu"
                visible: root.compactDockHeader
                text: "Tools"
                iconName: "settings"
                compact: true
                primaryOpensMenu: true
                menuWidth: 270
                menuItems: root.compactDockToolsMenuItems()
                onMenuItemTriggered: function(key) { root.triggerHeaderTool(key) }
            }
        }

        Flickable {
            objectName: "timelineAudioRoleBatchRail"
            visible: !root.compactHeightDock
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? audioRoleBatchRow.implicitHeight : 0
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

        RowLayout {
            id: mixerDockStripRow
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: root.compactHeightDock ? theme.space6 : theme.space8

            ScrollView {
                id: mixerStripsScroll
                objectName: "timelineMixerStripsViewport"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: theme.scaled(200)
                clip: true
                contentHeight: root.docked
                               ? root.compactHeightDock ? availableHeight
                                 : Math.max(availableHeight, theme.scaled(620))
                               : availableHeight
                ScrollBar.vertical.policy: root.docked ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                ScrollBar.horizontal.policy: ScrollBar.AsNeeded

                RowLayout {
                    height: mixerStripsScroll.contentHeight
                    spacing: root.compactWidthDock ? theme.space4 : theme.space8

                Rectangle {
                    objectName: "timelineSelectedClipMixerStrip"
                    visible: root.selectedClipAudioAvailable && !root.compactWidthDock
                    Layout.preferredWidth: root.compactHeightDock ? theme.scaled(168)
                                                                  : theme.scaled(190)
                    Layout.fillHeight: true
                    radius: theme.radiusSurface
                    color: theme.alpha(theme.ink850, 0.78)
                    border.width: 0

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: root.compactHeightDock ? theme.space4 : theme.space10
                        spacing: root.compactHeightDock ? theme.space2 : theme.space6

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
                            font.family: theme.fontNumeric
                            font.features: theme.numericFontFeatures
                            font.pixelSize: theme.typeMicro
                            elide: Text.ElideRight
                        }

                        InspectorSlider {
                            objectName: "timelineSelectedClipGainSlider"
                            Layout.fillWidth: true
                            label: "Clip gain"
                            valueText: Number(root.currentClip.baseAudioGainDb || 0).toFixed(1) + " dB"
                            from: -36
                            to: 24
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
                            visible: !root.compactHeightDock
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
                            visible: !root.compactHeightDock
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
                            visible: !root.compactHeightDock
                            text: "Clip automation and fades remain editable on the timeline."
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Repeater {
                    model: stableAudioTrackRows

                    delegate: Rectangle {
                        id: trackStrip
                        objectName: "timelineMixerTrackStrip_" + String(trackId || trackIndex)
                        required property var trackId
                        required property var trackIndex
                        required property var displayLabel
                        required property var audioBus
                        required property var audioRole
                        required property var audioGainDb
                        required property var audioPan
                        required property var muted
                        required property var solo
                        required property var audioCompressorEnabled
                        required property var audioDuckingEnabled
                        readonly property var modelData: ({
                            "trackId": trackId,
                            "trackIndex": trackIndex,
                            "displayLabel": displayLabel,
                            "audioBus": audioBus,
                            "audioRole": audioRole,
                            "audioGainDb": audioGainDb,
                            "audioPan": audioPan,
                            "muted": muted,
                            "solo": solo,
                            "audioCompressorEnabled": audioCompressorEnabled,
                            "audioDuckingEnabled": audioDuckingEnabled
                        })
                        readonly property int rowIndex: Number(modelData.trackIndex || 0)
                        readonly property bool selected: root.selectedAudioTrackIndex === rowIndex
                        Layout.preferredWidth: root.compactWidthDock ? theme.scaled(204)
                                              : root.compactHeightDock ? theme.scaled(240)
                                                                      : theme.scaled(202)
                        Layout.fillHeight: true
                        radius: theme.radiusSurface
                        color: selected ? theme.alpha(theme.selectionAccent, 0.12)
                                        : theme.alpha(theme.ink850, 0.78)
                        border.width: selected ? theme.hairlineWidth : 0
                        border.color: theme.selectionAccent

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: root.compactHeightDock ? theme.space4 : theme.space10
                            spacing: root.compactHeightDock ? theme.space2 : theme.space6

                            RowLayout {
                                visible: !root.compactHeightDock
                                Layout.fillWidth: true

                                Label {
                                    objectName: "timelineMixerTrackName_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    Layout.fillWidth: true
                                    text: String(trackStrip.modelData.displayLabel || "Audio")
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeControl
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                StatusChip {
                                    visible: !root.compactHeightDock
                                    text: String(trackStrip.modelData.audioBus || "main").toUpperCase()
                                    tone: "neutral"
                                    compact: true
                                    minimumReadableWidth: 46
                                    maximumReadableWidth: 72
                                }

                                FilterChip {
                                    objectName: "timelineMixerTrackSelect_" + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    Layout.preferredWidth: root.compactHeightDock ? theme.scaled(40)
                                                                              : theme.scaled(64)
                                    Layout.minimumWidth: root.compactHeightDock ? theme.scaled(36)
                                                                            : theme.scaled(52)
                                    text: root.compactHeightDock
                                          ? trackStrip.selected ? "✓" : "Sel"
                                          : trackStrip.selected ? "Selected" : "Select"
                                    active: trackStrip.selected
                                    tooltipText: trackStrip.selected
                                                  ? "This track is selected for contextual inserts."
                                                  : "Select this track for contextual inserts."
                                    onClicked: root.selectMixerTrack(trackStrip.rowIndex)
                                }

                            }

                            RowLayout {
                                visible: !root.compactHeightDock
                                Layout.fillWidth: true

                                FilterChip {
                                    objectName: "timelineMixerTrackMute_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    Layout.preferredWidth: root.compactHeightDock ? theme.scaled(38) : -1
                                    Layout.fillWidth: true
                                    text: "M"
                                    active: trackStrip.modelData.muted === true
                                    tone: active ? "warning" : "neutral"
                                    tooltipText: "Mute track"
                                    onClicked: root.timelineDocument.toggleTrackMuted(
                                                   Number(trackStrip.modelData.trackIndex || 0))
                                }

                                FilterChip {
                                    objectName: "timelineMixerTrackSolo_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    Layout.preferredWidth: root.compactHeightDock ? theme.scaled(38) : -1
                                    Layout.fillWidth: true
                                    text: "S"
                                    active: trackStrip.modelData.solo === true
                                    tone: active ? "accent" : "neutral"
                                    tooltipText: "Solo track"
                                    onClicked: root.timelineDocument.toggleTrackSolo(
                                                   Number(trackStrip.modelData.trackIndex || 0))
                                }

                                MenuButton {
                                    text: root.compactHeightDock ? "Tools" : "Inserts"
                                    compact: true
                                    primaryOpensMenu: true
                                    menuWidth: 258
                                    menuItems: root.trackToolsMenuItems(trackStrip.modelData)
                                    onMenuItemTriggered: function(key) {
                                        root.triggerTrackTool(key, trackStrip.modelData)
                                    }
                                }
                            }

                            RowLayout {
                                visible: root.compactHeightDock
                                Layout.fillWidth: true
                                spacing: theme.space2

                                Label {
                                    objectName: "timelineMixerCompactTrackName_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: theme.scaled(root.compactWidthDock ? 20 : 34)
                                    text: String(trackStrip.modelData.displayLabel || "Audio")
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeCaption
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                StatusChip {
                                    objectName: "timelineMixerCompactGainReadout_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    text: trackGainFader.value.toFixed(1) + " dB"
                                    tone: Math.abs(trackGainFader.value) > 0.01 ? "accent" : "neutral"
                                    compact: true
                                    dotVisible: false
                                    minimumReadableWidth: 72
                                    maximumReadableWidth: 72
                                }

                                FilterChip {
                                    objectName: "timelineMixerCompactMute_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    Layout.preferredWidth: theme.scaled(24)
                                    Layout.minimumWidth: theme.scaled(24)
                                    Layout.fillWidth: false
                                    leftPadding: theme.space4
                                    rightPadding: theme.space4
                                    text: "M"
                                    active: trackStrip.modelData.muted === true
                                    tone: active ? "warning" : "neutral"
                                    tooltipText: "Mute track"
                                    onClicked: root.timelineDocument.toggleTrackMuted(trackStrip.rowIndex)
                                }

                                FilterChip {
                                    objectName: "timelineMixerCompactSolo_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    Layout.preferredWidth: theme.scaled(24)
                                    Layout.minimumWidth: theme.scaled(24)
                                    Layout.fillWidth: false
                                    leftPadding: theme.space4
                                    rightPadding: theme.space4
                                    text: "S"
                                    active: trackStrip.modelData.solo === true
                                    tone: active ? "accent" : "neutral"
                                    tooltipText: "Solo track"
                                    onClicked: root.timelineDocument.toggleTrackSolo(trackStrip.rowIndex)
                                }

                                MenuButton {
                                    objectName: "timelineMixerCompactTrackTools_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    Layout.preferredWidth: theme.scaled(root.compactWidthDock ? 30 : 40)
                                    Layout.minimumWidth: theme.scaled(root.compactWidthDock ? 30 : 40)
                                    text: "FX"
                                    showText: true
                                    showMenuIndicator: !root.compactWidthDock
                                    iconName: ""
                                    accessibleName: "Audio effects and routing"
                                    tooltipText: "Track audio effects: EQ, compressor, limiter, gate, de-esser, filters and dialogue cleanup. Also includes routing."
                                    compact: true
                                    primaryOpensMenu: true
                                    menuWidth: 258
                                    menuItems: root.trackToolsMenuItems(trackStrip.modelData)
                                    onMenuItemTriggered: function(key) {
                                        root.triggerTrackTool(key, trackStrip.modelData)
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumHeight: root.compactHeightDock ? theme.scaled(120)
                                                                            : theme.scaled(214)
                                Layout.preferredHeight: root.compactHeightDock ? theme.scaled(124)
                                                                              : theme.scaled(244)
                                spacing: root.compactHeightDock ? theme.space3 : theme.space4

                                TimelineAudioMeter {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumWidth: root.compactHeightDock ? theme.scaled(64)
                                                                                : theme.scaled(98)
                                    Layout.preferredWidth: root.compactHeightDock ? theme.scaled(96)
                                                                                 : theme.scaled(112)
                                    theme: root.theme
                                    meter: root.meterForTrack(trackStrip.modelData.trackId)
                                    reducedMotion: root.reducedMotion
                                }

                                ColumnLayout {
                                    Layout.preferredWidth: root.compactHeightDock ? theme.scaled(38)
                                                                                 : theme.scaled(48)
                                    Layout.fillHeight: true
                                    spacing: theme.space2

                                    Label {
                                        Layout.fillWidth: true
                                        visible: !root.compactHeightDock
                                        text: "GAIN"
                                        color: theme.textFaint
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeMicro
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Slider {
                                        id: trackGainFader
                                        objectName: "timelineTrackGainFader_" + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        orientation: Qt.Vertical
                                        from: -60
                                        to: 24
                                        stepSize: 0.5
                                        value: root.trackAutomationValue(trackStrip.modelData, "audioGainDb")
                                        leftPadding: 0
                                        rightPadding: 0
                                        topPadding: theme.space8
                                        bottomPadding: theme.space8
                                        Accessible.name: String(trackStrip.modelData.displayLabel || "Audio")
                                                            + " track gain"
                                        Accessible.description: value.toFixed(1)
                                                                + " dB. Use the arrow keys for half-decibel steps."

                                        Binding {
                                            target: trackGainFader
                                            property: "value"
                                            value: root.trackAutomationValue(
                                                       trackStrip.modelData, "audioGainDb")
                                            when: !trackGainFader.pressed
                                        }

                                        background: Item {
                                            id: trackGainBackground
                                            x: trackGainFader.leftPadding
                                            y: trackGainFader.topPadding
                                            width: trackGainFader.availableWidth
                                            height: trackGainFader.availableHeight

                                            Rectangle {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                anchors.top: parent.top
                                                anchors.bottom: parent.bottom
                                                width: theme.scaled(4)
                                                radius: width / 2
                                                color: theme.alpha(theme.lineStrong, 0.42)
                                            }

                                            Rectangle {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                anchors.bottom: parent.bottom
                                                width: theme.scaled(4)
                                                height: parent.height
                                                        * (1 - trackGainFader.visualPosition)
                                                radius: width / 2
                                                color: theme.accentWarm400
                                                opacity: 0.74
                                            }

                                            Repeater {
                                                model: [
                                                    {"value": 24, "label": "+24"},
                                                    {"value": 0, "label": "0"},
                                                    {"value": -24, "label": "−24"},
                                                    {"value": -60, "label": "−60"}
                                                ]

                                                delegate: Rectangle {
                                                    required property var modelData
                                                    readonly property real position:
                                                        1 - (Number(modelData.value) - trackGainFader.from)
                                                        / (trackGainFader.to - trackGainFader.from)
                                                    x: trackGainBackground.width / 2 - width / 2
                                                    y: Math.max(0, Math.min(trackGainBackground.height - height,
                                                        position * trackGainBackground.height - height / 2))
                                                    width: theme.scaled(7)
                                                    height: theme.scaled(1)
                                                    color: theme.alpha(theme.textMuted, 0.68)
                                                }
                                            }
                                        }

                                        handle: Rectangle {
                                            objectName: "timelineTrackGainHandle_"
                                                         + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                            implicitWidth: theme.scaled(36)
                                            implicitHeight: theme.scaled(13)
                                            x: (trackGainFader.width - width) / 2
                                            y: trackGainFader.topPadding
                                               + trackGainFader.visualPosition
                                               * (trackGainFader.availableHeight - height)
                                            radius: theme.radiusControl
                                            color: trackGainFader.activeFocus
                                                   ? theme.polar : theme.alpha(theme.polar, 0.92)
                                            border.width: trackGainFader.activeFocus ? 2 : 1
                                            border.color: trackGainFader.activeFocus
                                                          ? theme.focusRing : theme.alpha(theme.accentWarm400, 0.84)
                                        }

                                        onPressedChanged: {
                                            if (pressed)
                                                root.timelineDocument.beginContinuousEdit()
                                            else
                                                root.timelineDocument.endContinuousEdit()
                                        }
                                        onMoved: root.timelineDocument.setTrackAutomationValue(
                                                     trackStrip.rowIndex, "audioGainDb", value)
                                        Keys.onPressed: function(event) {
                                            let nextValue = value
                                            if (event.key === Qt.Key_Up || event.key === Qt.Key_Right)
                                                nextValue += event.modifiers & Qt.ShiftModifier
                                                             ? stepSize * 10 : stepSize
                                            else if (event.key === Qt.Key_Down || event.key === Qt.Key_Left)
                                                nextValue -= event.modifiers & Qt.ShiftModifier
                                                             ? stepSize * 10 : stepSize
                                            else if (event.key === Qt.Key_PageUp)
                                                nextValue += stepSize * 10
                                            else if (event.key === Qt.Key_PageDown)
                                                nextValue -= stepSize * 10
                                            else if (event.key === Qt.Key_Home)
                                                nextValue = from
                                            else if (event.key === Qt.Key_End)
                                                nextValue = to
                                            else
                                                return

                                            nextValue = Math.max(from, Math.min(to, nextValue))
                                            if (Math.abs(nextValue - value) > 0.0001) {
                                                root.timelineDocument.beginContinuousEdit()
                                                root.timelineDocument.setTrackAutomationValue(
                                                            trackStrip.rowIndex, "audioGainDb", nextValue)
                                                root.timelineDocument.endContinuousEdit()
                                            }
                                            event.accepted = true
                                        }
                                    }

                                    ToneTextField {
                                        numericEntry: true
                                        id: trackGainValueField
                                        objectName: "timelineTrackGainReadout_" + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                        Layout.fillWidth: true
                                        visible: !root.compactHeightDock
                                        Layout.preferredHeight: theme.controlHeightDense
                                        accessibleLabel: String(trackStrip.modelData.displayLabel || "Audio")
                                                         + " track gain in decibels"
                                        placeholderText: "dB"
                                        text: trackGainFader.value.toFixed(1) + " dB"
                                        horizontalAlignment: Text.AlignHCenter
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                        property bool gainValueDirty: false

                                        function restoreGainValueText() {
                                            text = Qt.binding(function() {
                                                return trackGainFader.value.toFixed(1) + " dB"
                                            })
                                        }

                                        function commitGainValue() {
                                            if (!gainValueDirty)
                                                return false
                                            const match = String(text).trim().match(
                                                /^([+-]?(?:\d+(?:\.\d*)?|\.\d+))\s*(?:dB)?$/i)
                                            const proposed = match ? Number(match[1]) : NaN
                                            gainValueDirty = false
                                            if (!Number.isFinite(proposed)) {
                                                restoreGainValueText()
                                                return false
                                            }
                                            const nextValue = Math.max(trackGainFader.from,
                                                Math.min(trackGainFader.to, proposed))
                                            root.timelineDocument.beginContinuousEdit()
                                            root.timelineDocument.setTrackAutomationValue(
                                                trackStrip.rowIndex, "audioGainDb", nextValue)
                                            root.timelineDocument.endContinuousEdit()
                                            restoreGainValueText()
                                            return true
                                        }

                                        onTextEdited: gainValueDirty = true
                                        onAccepted: commitGainValue()
                                        onActiveFocusChanged: {
                                            if (!activeFocus)
                                                commitGainValue()
                                        }
                                    }

                                    ActionButton {
                                        objectName: "timelineTrackGainReset_" + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                        Layout.fillWidth: true
                                        visible: !root.compactHeightDock
                                        text: "0 dB"
                                        iconName: "reset"
                                        compact: true
                                        tooltipText: "Reset the track fader to unity gain."
                                        enabled: Math.abs(trackGainFader.value) > 0.01
                                        onClicked: {
                                            root.timelineDocument.beginContinuousEdit()
                                            root.timelineDocument.setTrackAutomationValue(
                                                        trackStrip.rowIndex, "audioGainDb", 0)
                                            root.timelineDocument.endContinuousEdit()
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                objectName: "timelineMixerTrackPan_"
                                             + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.compactHeightDock
                                                        ? theme.scaled(34) : -1
                                spacing: theme.space2

                                Label {
                                    visible: root.compactHeightDock
                                    Layout.preferredWidth: theme.scaled(24)
                                    text: "PAN"
                                    color: theme.textFaint
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeMicro
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    Accessible.ignored: true
                                }

                                Slider {
                                    id: compactTrackPanSlider
                                    objectName: "timelineMixerCompactPanSlider_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    visible: root.compactHeightDock
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumWidth: root.compactHeightDock
                                                        ? theme.scaled(72) : 0
                                    orientation: Qt.Horizontal
                                    from: -1
                                    to: 1
                                    stepSize: 0.01
                                    value: root.trackAutomationValue(trackStrip.modelData, "audioPan")
                                    leftPadding: 0
                                    rightPadding: 0
                                    topPadding: 0
                                    bottomPadding: 0
                                    Accessible.name: String(trackStrip.modelData.displayLabel || "Audio")
                                                        + " track pan"
                                    Accessible.description: root.compactPanDescription(trackStrip.modelData)

                                    Binding {
                                        target: compactTrackPanSlider
                                        property: "value"
                                        value: root.trackAutomationValue(trackStrip.modelData, "audioPan")
                                        when: !compactTrackPanSlider.pressed
                                    }

                                    background: Item {
                                        x: compactTrackPanSlider.leftPadding
                                        y: compactTrackPanSlider.topPadding
                                           + (compactTrackPanSlider.availableHeight - height) / 2
                                        width: compactTrackPanSlider.availableWidth
                                        height: theme.scaled(4)

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: height / 2
                                            color: theme.alpha(theme.lineStrong, 0.58)
                                        }

                                        Rectangle {
                                            x: Math.min(parent.width / 2,
                                                parent.width * compactTrackPanSlider.visualPosition)
                                            width: Math.abs(parent.width
                                                * compactTrackPanSlider.visualPosition
                                                - parent.width / 2)
                                            height: parent.height
                                            radius: height / 2
                                            color: theme.alpha(theme.accentWarm400, 0.84)
                                        }

                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: theme.hairlineWidth
                                            height: theme.scaled(10)
                                            radius: width / 2
                                            color: theme.textMuted
                                        }
                                    }

                                    handle: Rectangle {
                                        objectName: "timelineMixerCompactPanHandle_"
                                                     + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                        implicitWidth: theme.scaled(12)
                                        implicitHeight: theme.scaled(18)
                                        radius: theme.radiusControl
                                        color: compactTrackPanSlider.activeFocus
                                               ? theme.polar : theme.alpha(theme.polar, 0.94)
                                        border.width: compactTrackPanSlider.activeFocus ? 2 : 1
                                        border.color: compactTrackPanSlider.activeFocus
                                                      ? theme.focusRing
                                                      : theme.alpha(theme.accentWarm400, 0.84)
                                    }

                                    onPressedChanged: {
                                        if (pressed)
                                            root.timelineDocument.beginContinuousEdit()
                                        else
                                            root.timelineDocument.endContinuousEdit()
                                    }
                                    onMoved: root.timelineDocument.setTrackAutomationValue(
                                                 trackStrip.rowIndex, "audioPan", value)
                                    Keys.onPressed: function(event) {
                                        let nextValue = value
                                        const increment = event.modifiers & Qt.ShiftModifier
                                                          ? stepSize * 10 : stepSize
                                        if (event.key === Qt.Key_Left || event.key === Qt.Key_Down)
                                            nextValue -= increment
                                        else if (event.key === Qt.Key_Right || event.key === Qt.Key_Up)
                                            nextValue += increment
                                        else if (event.key === Qt.Key_Home)
                                            nextValue = 0
                                        else if (event.key === Qt.Key_End)
                                            nextValue = event.modifiers & Qt.ShiftModifier ? 1 : -1
                                        else
                                            return

                                        nextValue = Math.max(from, Math.min(to, nextValue))
                                        if (Math.abs(nextValue - value) > 0.0001) {
                                            root.timelineDocument.beginContinuousEdit()
                                            root.timelineDocument.setTrackAutomationValue(
                                                trackStrip.rowIndex, "audioPan", nextValue)
                                            root.timelineDocument.endContinuousEdit()
                                        }
                                        event.accepted = true
                                    }
                                }

                                Label {
                                    objectName: "timelineMixerCompactPanReadout_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    visible: root.compactHeightDock
                                    Layout.preferredWidth: theme.scaled(34)
                                    text: root.compactPanReadout(trackStrip.modelData)
                                    color: theme.textMain
                                    font.family: theme.fontNumeric
                                    font.features: theme.numericFontFeatures
                                    font.pixelSize: theme.typeMicro
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    Accessible.name: root.compactPanDescription(trackStrip.modelData)
                                }

                                InspectorSlider {
                                    objectName: "timelineMixerTrackPanInspector_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    visible: !root.compactHeightDock
                                    Layout.fillWidth: true
                                    label: "Pan"
                                    valueText: Math.abs(root.trackAutomationValue(
                                                            trackStrip.modelData, "audioPan")) < 0.005
                                               ? "Center"
                                               : root.trackAutomationValue(
                                                     trackStrip.modelData, "audioPan") < 0
                                                 ? "L " + Math.round(Math.abs(root.trackAutomationValue(
                                                     trackStrip.modelData, "audioPan")) * 100)
                                                 : "R " + Math.round(root.trackAutomationValue(
                                                     trackStrip.modelData, "audioPan") * 100)
                                    from: -1
                                    to: 1
                                    stepSize: 0.01
                                    value: root.trackAutomationValue(
                                               trackStrip.modelData, "audioPan")
                                    defaultValue: 0
                                    valueInputMode: "raw"
                                    valueColumnWidth: theme.inspectorValueWidth
                                    controlHeight: theme.scaled(theme.densityComfortable ? 62
                                                   : theme.densityCompact ? 50 : 56)
                                    Layout.preferredHeight: implicitHeight
                                    showResetButton: !root.compactHeightDock
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

                                FilterChip {
                                    objectName: "timelineMixerCompactTrackSelect_"
                                                 + String(trackStrip.modelData.trackId || trackStrip.rowIndex)
                                    visible: root.compactHeightDock
                                    Layout.preferredWidth: theme.scaled(52)
                                    Layout.minimumWidth: theme.scaled(52)
                                    Layout.fillWidth: false
                                    leftPadding: theme.space4
                                    rightPadding: theme.space4
                                    text: trackStrip.selected ? "✓" : "Select"
                                    Accessible.name: trackStrip.selected ? "Track selected" : "Select track"
                                    active: trackStrip.selected
                                    tooltipText: trackStrip.selected
                                                  ? "This track is selected for contextual inserts."
                                                  : "Select this track for contextual inserts."
                                    onClicked: root.selectMixerTrack(trackStrip.rowIndex)
                                }
                            }

                            ToneComboBox {
                                objectName: "timelineTrackAudioRoleCombo"
                                visible: !root.compactHeightDock
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
                                visible: !root.compactHeightDock
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

            }
            }

            Rectangle {
                objectName: "timelineMasterMixerStrip"
                Layout.preferredWidth: root.compactWidthDock ? theme.scaled(148)
                                      : root.compactHeightDock ? theme.scaled(136)
                                                              : theme.scaled(180)
                Layout.minimumWidth: root.compactWidthDock ? theme.scaled(140)
                                     : root.compactHeightDock ? theme.scaled(124)
                                                            : theme.scaled(160)
                Layout.fillHeight: true
                radius: theme.radiusSurface
                color: theme.alpha(theme.ink750, 0.78)
                border.width: 0

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: root.compactHeightDock ? theme.space4 : theme.space10
                    spacing: root.compactHeightDock ? theme.space2 : theme.space6

                    Label {
                        text: root.compactHeightDock ? "MASTER" : "MASTER OUTPUT"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: root.compactHeightDock ? theme.typeCaption : theme.typeControl
                        font.weight: Font.DemiBold
                    }

                    StatusChip {
                        visible: !root.compactHeightDock
                        text: root.masterMeter.overloadLatched === true ? "OVERLOAD" : "PROGRAM"
                        tone: root.masterMeter.overloadLatched === true ? "danger" : "neutral"
                        compact: true
                    }

                    TimelineAudioMeter {
                        objectName: "timelineMasterAudioMeter"
                        Layout.alignment: root.compactWidthDock ? Qt.AlignTop : Qt.AlignHCenter
                        Layout.fillWidth: root.compactWidthDock
                        Layout.fillHeight: true
                        Layout.minimumWidth: root.compactWidthDock ? 1 : theme.scaled(90)
                        theme: root.theme
                        meter: root.masterMeter
                        reducedMotion: root.reducedMotion
                        loudnessText: "M " + root.momentaryLufs.toFixed(1)
                                      + "\nS " + root.shortTermLufs.toFixed(1) + " LUFS"
                    }

                    ActionButton {
                        visible: !root.compactHeightDock
                        Layout.fillWidth: true
                        text: root.timelineDocument.loudnessAnalysisBusy
                              ? "Analyzing…" : "Measure LUFS"
                        iconName: "audio"
                        compact: true
                        enabled: !root.timelineDocument.loudnessAnalysisBusy
                        onClicked: root.timelineDocument.analyzeTimelineLoudness()
                    }

                    ActionButton {
                        visible: !root.compactHeightDock
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

    TimelineVoiceoverPanel {
        id: timelineVoiceoverPanel
        recorder: root.voiceoverRecorder
        timelineDocument: root.timelineDocument
        onPlaybackStartRequested: root.startVoiceoverPlayback()
        onPlaybackStopRequested: root.stopVoiceoverPlayback()
    }

    AppPopup {
        id: timelineCompactSelectedClipToolsPopup
        objectName: "timelineCompactSelectedClipToolsWindow"
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, theme.scaled(360))
        height: Math.min(popupAvailableHeight, theme.scaled(420))
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space6

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        text: "Selected Clip"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeSection
                        font.weight: Font.DemiBold
                    }

                    Label {
                        objectName: "timelineAudioChannelPopupTitle"
                        Layout.fillWidth: true
                        text: String(root.currentClip.displayLabel || "Audio clip") + " · pre-fader"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        elide: Text.ElideRight
                    }
                }

                ActionButton {
                    showText: false
                    compact: true
                    iconName: "close"
                    accessibleName: "Close selected clip audio controls"
                    tooltipText: "Close selected clip audio controls."
                    onClicked: timelineCompactSelectedClipToolsPopup.close()
                }
            }

            ActionButton {
                objectName: "timelineCompactSelectedClipChannelMappingButton"
                Layout.fillWidth: true
                text: "Source channel mapping"
                iconName: "audio"
                compact: true
                tooltipText: "Map source channels before clip gain, fades, track processing, and export."
                onClicked: timelineAudioChannelMappingPopup.open()
            }

            ScrollView {
                id: timelineCompactSelectedClipControlsViewport
                objectName: "timelineCompactSelectedClipControlsViewport"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: timelineCompactSelectedClipControlsViewport.availableWidth
                    spacing: theme.space4

                    InspectorSlider {
                        objectName: "timelineCompactSelectedClipGainSlider"
                        Layout.fillWidth: true
                        label: "Clip gain"
                        valueText: Number(root.currentClip.baseAudioGainDb || 0).toFixed(1) + " dB"
                        from: -36
                        to: 24
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
                        objectName: "timelineCompactSelectedClipFadeInSlider"
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
                        objectName: "timelineCompactSelectedClipFadeOutSlider"
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
                }
            }

            Label {
                Layout.fillWidth: true
                text: "Clip gain and fades are undoable and remain editable on the timeline."
                color: theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.Wrap
            }
        }
    }

    AppPopup {
        id: timelineAudioChannelMappingPopup
        objectName: "timelineAudioChannelMappingWindow"
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, theme.scaled(520))
        height: Math.min(popupAvailableHeight, theme.scaled(470))
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        padding: standardPopupPadding
        closePolicy: Popup.CloseOnEscape

        background: PopupSurface {}

        contentItem: ColumnLayout {
            spacing: theme.space10

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: theme.space2

                    Label {
                        objectName: "timelineAudioChannelPopupTitle"
                        Layout.fillWidth: true
                        text: "Source Channels · " + String(root.currentClip.displayLabel || "Selected")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }

                    Label {
                        objectName: "timelineAudioChannelPopupSummary"
                        Layout.fillWidth: true
                        text: "Routing happens before clip gain, fades, track processing, and export."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
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
                        objectName: "timelineAudioSourceLayoutReadout"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: String(root.currentClip.sourceAudioChannelLayout || "Source layout will appear after media analysis")
                        color: theme.textMain
                        font.family: theme.fontNumeric
                        font.features: theme.numericFontFeatures
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
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
                    Layout.minimumWidth: 0
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
                        font.family: theme.fontNumeric
                        font.features: theme.numericFontFeatures
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
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, theme.scaled(560))
        height: Math.min(popupAvailableHeight, theme.scaled(650))
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
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
                    Layout.minimumWidth: 0
                    spacing: theme.space2

                    Label {
                        Layout.fillWidth: true
                        text: "Dialogue Cleanup"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: String(root.processorTrack().displayLabel || "Audio")
                              + " · live preview, one undoable Apply"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
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
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(popupAvailableWidth, theme.scaled(620))
        height: Math.min(popupAvailableHeight, theme.scaled(640))
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
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
                    Layout.minimumWidth: 0
                    spacing: theme.space2

                    Label {
                        Layout.fillWidth: true
                        text: "Processing · " + String(root.processorTrack().displayLabel || "Audio")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "A compact, ordered track chain. Diamonds write automation at the playhead."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
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
                                    tooltipText: String(processorRow.modelData.scope || "")
                                                 + (processorRow.modelData.scope ? "\n" : "")
                                                 + (active ? "Disable this processor." : "Enable this processor.")
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
