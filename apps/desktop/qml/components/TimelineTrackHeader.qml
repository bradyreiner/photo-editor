pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property var trackData
    required property var timelineDocument
    property bool meterVisible: false
    property real meterLeft: 0
    property real meterRight: 0
    property bool meterClipping: false
    property bool automationLaneExpanded: false
    property string automationLaneParameter: trackData.trackKind === "audio" ? "audioGainDb" : "opacity"
    property int reorderPreviewDirection: 0

    signal resizeRequested(real delta)
    signal heightPresetRequested(real height)
    signal automationLaneRequested(string parameter)

    readonly property var documentController: timelineDocument !== undefined && timelineDocument !== null ? timelineDocument : null
    // These methods are part of the required controller contract. QML does
    // not consistently expose C++ invokables as JavaScript `function` values,
    // so `typeof` checks incorrectly disabled every track control at runtime.
    readonly property bool canToggleLocked: documentController !== null
    readonly property bool canToggleMuted: documentController !== null
    readonly property bool canToggleSolo: documentController !== null
    readonly property bool canToggleVisibility: documentController !== null
    readonly property bool canToggleTargeted: documentController !== null
    readonly property bool canTogglePrimary: trackData.trackKind === "video" ? canToggleVisibility : canToggleMuted
    readonly property bool isMediaTrack: String(trackData.trackRole || "media") !== "caption"
    readonly property int pairedLaneIndex: Number(trackData.laneIndex || 0)
    readonly property bool canMoveEarlier: isMediaTrack && pairedLaneIndex > 0
    readonly property bool canMoveLater: isMediaTrack
                                                 && documentController !== null
                                                 && pairedLaneIndex < Number(documentController.videoTrackCount) - 1
    readonly property bool canDeleteTrack: isMediaTrack
                                           && documentController !== null
                                           && Number(documentController.videoTrackCount) > 1

    function togglePrimaryTrackState() {
        if (trackData.trackKind === "video") {
            if (canToggleVisibility)
                documentController.toggleTrackVisibility(trackData.trackIndex)
            return
        }

        if (canToggleMuted)
            documentController.toggleTrackMuted(trackData.trackIndex)
    }

    function toggleSoloTrackState() {
        if (canToggleSolo)
            documentController.toggleTrackSolo(trackData.trackIndex)
    }

    function toggleTargetedTrackState() {
        if (canToggleTargeted)
            documentController.toggleTrackTargeted(trackData.trackIndex)
    }

    function toggleLockedTrackState() {
        if (canToggleLocked)
            documentController.toggleTrackLocked(trackData.trackIndex)
    }

    readonly property string automationParameter: trackData.trackKind === "audio" ? "audioGainDb" : "opacity"
    readonly property real automationValue: trackData.trackKind === "audio"
                                            ? Number(trackData.audioGainDb || 0)
                                            : Number(trackData.opacity !== undefined ? trackData.opacity : 1)
    readonly property real panValue: Number(trackData.audioPan || 0)
    readonly property color patchAccent: trackData.trackKind === "audio"
                                         ? theme.audioAccent
                                         : theme.accentCool500

    function hasAutomationKeyAtPlayhead(parameterName) {
        const resolvedParameter = String(parameterName || automationParameter)
        const values = trackData.automationKeyframes || []
        const playhead = Number(documentController ? documentController.sequencePlayheadSeconds : 0)
        const tolerance = 0.5 / Math.max(1, Number(documentController ? documentController.framesPerSecond : 30))
        for (let index = 0; index < values.length; ++index) {
            if (String(values[index].parameter || "") === resolvedParameter
                    && Math.abs(Number(values[index].seconds || 0) - playhead) <= tolerance)
                return true
        }
        return false
    }

    function audioBusIndex(busName) {
        const normalized = String(busName || "main").toLowerCase()
        if (normalized === "dialogue") return 1
        if (normalized === "music") return 2
        if (normalized === "sfx") return 3
        return 0
    }

    function automationParameterLabel(parameterName) {
        if (parameterName === "audioGainDb") return "Gain"
        if (parameterName === "audioPan") return "Pan"
        return "Opacity"
    }

    readonly property string timelineTrackHeaderStyleContract: "timeline-track-header-style:v3|surface:quiet-editorial|padding:compact|type:shared|targets:32|states:patched-locked-muted-solo-hidden|patch:independent-exclusive-stream-destination|target:accent-rail|edges:structural-only|resize:intrinsic"
    readonly property string timelineTrackMixerContract: "timeline-track-mixer:v3|popup:gain-pan-processing|automation:inline-lanes-contextual|routing:main-dialogue-music-sfx|processing:progressive-three-band-eq-compressor-ducking|meters:contextual-stereo-peak-clip|default-density:unchanged|controls:shared"

    radius: theme.radius4
    color: trackData.targeted
           ? theme.alpha(root.patchAccent, 0.09)
           : theme.alpha(theme.ink950, 0.28)
    border.width: 0

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: theme.scaled(2)
        radius: width / 2
        visible: trackData.targeted
        color: root.patchAccent
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: theme.hairlineWidth
        color: theme.alpha(theme.lineSoft, 0.22)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: theme.hairlineWidth
        color: theme.alpha(theme.lineSoft, 0.18)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.height < theme.scaled(56) ? theme.space4 : theme.space6
        spacing: theme.space4

        RowLayout {
            visible: root.height >= theme.scaled(56)
            Layout.fillWidth: true
            spacing: theme.space6

            Label {
                Layout.fillWidth: true
                text: trackData.displayLabel
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: theme.typeLabel
                font.weight: Font.DemiBold
            }

            Item {
                objectName: "timelineTrackStereoMeter"
                visible: root.meterVisible && trackData.trackKind === "audio"
                Layout.preferredWidth: visible ? theme.space8 : 0
                Layout.preferredHeight: theme.space14

                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    width: theme.scaled(2)
                    height: Math.max(theme.scaled(1), parent.height * Math.min(1, root.meterLeft))
                    radius: width / 2
                    color: root.meterClipping ? theme.error : theme.success
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: theme.scaled(2)
                    height: Math.max(theme.scaled(1), parent.height * Math.min(1, root.meterRight))
                    radius: width / 2
                    color: root.meterClipping ? theme.error : theme.success
                }
            }

            Label {
                text: trackData.trackKind === "audio" ? "Audio" : "Video"
                color: trackData.targeted ? root.patchAccent : theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
            }

            Item {
                id: trackReorderHandle
                objectName: "timelineTrackReorderHandle"
                Layout.preferredWidth: theme.space16
                Layout.preferredHeight: theme.space16
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Reorder " + trackData.displayLabel + " track pair"

                Keys.onUpPressed: {
                    if (root.canMoveEarlier)
                        documentController.moveTrackRow(trackData.trackIndex, -1)
                }
                Keys.onDownPressed: {
                    if (root.canMoveLater)
                        documentController.moveTrackRow(trackData.trackIndex, 1)
                }

                Column {
                    anchors.centerIn: parent
                    spacing: theme.scaled(2)

                    Repeater {
                        model: 3

                        Rectangle {
                            width: theme.space10
                            height: theme.hairlineWidth
                            radius: height / 2
                            color: trackReorderMouse.containsMouse
                                   || trackReorderHandle.activeFocus
                                   ? root.patchAccent : theme.textMuted
                        }
                    }
                }

                MouseArea {
                    id: trackReorderMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    property real pressGlobalY: 0

                    onPressed: function(mouse) {
                        trackReorderHandle.forceActiveFocus()
                        pressGlobalY = mapToGlobal(mouse.x, mouse.y).y
                        root.reorderPreviewDirection = 0
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        const delta = mapToGlobal(mouse.x, mouse.y).y - pressGlobalY
                        if (delta <= -theme.space12 && root.canMoveEarlier)
                            root.reorderPreviewDirection = -1
                        else if (delta >= theme.space12 && root.canMoveLater)
                            root.reorderPreviewDirection = 1
                        else
                            root.reorderPreviewDirection = 0
                    }
                    onReleased: {
                        const direction = root.reorderPreviewDirection
                        root.reorderPreviewDirection = 0
                        if (direction !== 0)
                            documentController.moveTrackRow(trackData.trackIndex, direction)
                    }
                    onCanceled: root.reorderPreviewDirection = 0
                }
            }
        }

        RowLayout {
            spacing: theme.space4

            TimelineTrackToggleButton {
                label: trackData.trackKind === "video"
                       ? (trackData.visible ? "Hide Video Track" : "Show Video Track")
                       : (trackData.muted ? "Unmute Audio Track" : "Mute Audio Track")
                shortLabel: trackData.trackKind === "video" ? "V" : "M"
                iconName: trackData.trackKind === "video"
                          ? (trackData.visible ? "visible" : "hidden")
                          : (trackData.muted ? "audio-muted" : "audio")
                checked: trackData.trackKind === "video" ? trackData.visible : trackData.muted
                checkedColor: trackData.trackKind === "video" ? theme.accentCool500 : theme.error
                enabled: canTogglePrimary
                onToggled: root.togglePrimaryTrackState()
            }

            TimelineTrackToggleButton {
                visible: trackData.trackKind === "audio"
                label: trackData.solo ? "Clear Solo" : "Solo Track"
                shortLabel: "S"
                iconName: "star"
                checked: trackData.solo
                checkedColor: theme.audioAccent
                enabled: canToggleSolo
                onToggled: root.toggleSoloTrackState()
            }

            TimelineTrackToggleButton {
                label: trackData.targeted
                       ? "Disable source " + trackData.trackKind + " patch to " + trackData.displayLabel
                       : "Patch source " + trackData.trackKind + " to " + trackData.displayLabel
                shortLabel: trackData.displayLabel
                iconName: "source"
                checked: trackData.targeted
                checkedColor: root.patchAccent
                enabled: canToggleTargeted
                onToggled: root.toggleTargetedTrackState()
            }

            TimelineTrackToggleButton {
                label: trackData.locked ? "Unlock Track" : "Lock Track"
                shortLabel: "L"
                iconName: "lock"
                checked: trackData.locked
                checkedColor: theme.warning
                enabled: canToggleLocked
                onToggled: root.toggleLockedTrackState()
            }
        }

        RowLayout {
            objectName: "timelineTrackAutomationHeader"
            visible: root.automationLaneExpanded
            Layout.fillWidth: true
            spacing: theme.space4

            Label {
                Layout.fillWidth: true
                text: root.automationParameterLabel(root.automationLaneParameter).toUpperCase()
                color: theme.accentCool300
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                font.weight: Font.DemiBold
            }

            ActionButton {
                showText: false
                compact: true
                iconName: root.hasAutomationKeyAtPlayhead(root.automationLaneParameter)
                          ? "keyframe-filled" : "keyframe"
                active: root.hasAutomationKeyAtPlayhead(root.automationLaneParameter)
                tooltipText: root.hasAutomationKeyAtPlayhead(root.automationLaneParameter)
                             ? "Remove the automation point at the playhead."
                             : "Add an automation point at the playhead."
                onClicked: documentController.toggleTrackAutomationKeyframe(
                               trackData.trackIndex, root.automationLaneParameter)
            }

            ActionButton {
                showText: false
                compact: true
                iconName: "close"
                tooltipText: "Close the inline automation lane."
                onClicked: root.automationLaneRequested(root.automationLaneParameter)
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                trackMenu.popup()
                mouse.accepted = true
            }
        }
    }

    Rectangle {
        objectName: "timelineTrackReorderPreview"
        anchors.left: parent.left
        anchors.right: parent.right
        y: root.reorderPreviewDirection < 0 ? 0 : parent.height - height
        height: theme.scaled(2)
        visible: root.reorderPreviewDirection !== 0
        color: root.patchAccent
        z: 30
    }

    Rectangle {
        anchors.centerIn: parent
        width: reorderPreviewLabel.implicitWidth + theme.space12
        height: reorderPreviewLabel.implicitHeight + theme.space6
        radius: theme.radiusChip
        visible: root.reorderPreviewDirection !== 0
        color: theme.alpha(theme.ink950, 0.92)
        z: 31

        Label {
            id: reorderPreviewLabel
            anchors.centerIn: parent
            text: root.reorderPreviewDirection < 0 ? "MOVE UP" : "MOVE DOWN"
            color: root.patchAccent
            font.family: theme.fontMono
            font.pixelSize: theme.typeMicro
            font.weight: Font.Bold
        }
    }

    Rectangle {
        id: trackHeightResizeHandle
        objectName: "timelineTrackHeightResizeHandle-" + String(trackData.trackId || trackData.displayLabel || "track")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 4
        color: resizeHandle.containsMouse || resizeHandle.pressed || activeFocus
               ? theme.focusRing : "transparent"
        // Four logical pixels is an intrinsic resize affordance, not panel chrome.
        radius: height / 2
        activeFocusOnTab: true
        Accessible.role: Accessible.Slider
        Accessible.name: "Resize " + String(trackData.displayLabel || "track") + " height"
        Accessible.description: "Use Up and Down Arrow to resize this track."
        Keys.onUpPressed: root.resizeRequested(-8)
        Keys.onDownPressed: root.resizeRequested(8)

        MouseArea {
            id: resizeHandle
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            hoverEnabled: true
            property real startMouseY: 0

            onPressed: function(mouse) {
                trackHeightResizeHandle.forceActiveFocus(Qt.MouseFocusReason)
                startMouseY = mapToGlobal(mouse.x, mouse.y).y
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const currentGlobalY = mapToGlobal(mouse.x, mouse.y).y
                const delta = currentGlobalY - startMouseY
                startMouseY = currentGlobalY
                root.resizeRequested(delta)
            }
        }
    }

    AppMenu {
        id: trackMenu
        accent: trackData.trackKind === "video" ? theme.accentCool500 : theme.audioAccent

        AppMenuItem {
            text: trackData.locked ? "Unlock Track" : "Lock Track"
            iconName: "lock"
            accent: trackMenu.accent
            enabled: canToggleLocked
            onTriggered: root.toggleLockedTrackState()
        }

        AppMenuItem {
            text: trackData.targeted
                  ? "Disable Source Patch"
                  : "Patch Source to " + trackData.displayLabel
            iconName: "check"
            accent: trackMenu.accent
            enabled: canToggleTargeted
            onTriggered: root.toggleTargetedTrackState()
        }

        AppMenuItem {
            visible: trackData.trackKind === "video"
            text: trackData.visible ? "Hide Video Track" : "Show Video Track"
            iconName: trackData.visible ? "hidden" : "visible"
            accent: trackMenu.accent
            enabled: canToggleVisibility
            onTriggered: root.togglePrimaryTrackState()
        }

        AppMenuItem {
            visible: trackData.trackKind === "audio"
            text: trackData.muted ? "Unmute Audio Track" : "Mute Audio Track"
            iconName: "audio"
            accent: trackMenu.accent
            enabled: canToggleMuted
            onTriggered: root.togglePrimaryTrackState()
        }

        AppMenuItem {
            visible: trackData.trackKind === "audio"
            text: trackData.solo ? "Clear Solo" : "Solo Track"
            iconName: "star"
            accent: trackMenu.accent
            enabled: canToggleSolo
            onTriggered: root.toggleSoloTrackState()
        }

        MenuSeparator {}

        AppMenuItem {
            objectName: "timelineMoveTrackEarlierAction"
            text: "Move Track Up"
            shortcutText: "⌥↑"
            accent: trackMenu.accent
            enabled: root.canMoveEarlier
            onTriggered: documentController.moveTrackRow(trackData.trackIndex, -1)
        }

        AppMenuItem {
            objectName: "timelineMoveTrackLaterAction"
            text: "Move Track Down"
            shortcutText: "⌥↓"
            accent: trackMenu.accent
            enabled: root.canMoveLater
            onTriggered: documentController.moveTrackRow(trackData.trackIndex, 1)
        }

        AppMenuItem {
            objectName: "timelineDeleteTrackAction"
            text: Number(trackData.clipCount || 0) > 0 ? "Delete Track and Clips…" : "Delete Track"
            danger: true
            enabled: root.canDeleteTrack
            onTriggered: {
                if (Number(trackData.clipCount || 0) > 0)
                    deleteTrackConfirmation.open()
                else
                    documentController.removeTrackRow(trackData.trackIndex, false)
            }
        }

        MenuSeparator {}

        AppMenuItem {
            text: "Minimize Track"
            accent: trackMenu.accent
            onTriggered: root.heightPresetRequested(40)
        }

        AppMenuItem {
            text: "Standard Track Height"
            accent: trackMenu.accent
            onTriggered: root.heightPresetRequested(62)
        }

        AppMenuItem {
            text: "Large Track Height"
            accent: trackMenu.accent
            onTriggered: root.heightPresetRequested(96)
        }

        AppMenuItem {
            text: trackData.trackKind === "audio" ? "Track mixer…" : "Track opacity automation…"
            iconName: "curve"
            accent: trackMenu.accent
            onTriggered: automationPopup.open()
        }

        AppMenuItem {
            visible: trackData.trackKind === "video"
            text: root.automationLaneExpanded && root.automationLaneParameter === "opacity"
                  ? "Hide Opacity Lane" : "Show Opacity Lane"
            iconName: "curve"
            accent: theme.accentCool400
            onTriggered: root.automationLaneRequested("opacity")
        }

        AppMenuItem {
            visible: trackData.trackKind === "audio"
            text: root.automationLaneExpanded && root.automationLaneParameter === "audioGainDb"
                  ? "Hide Gain Lane" : "Show Gain Lane"
            iconName: "curve"
            accent: theme.accentCool400
            onTriggered: root.automationLaneRequested("audioGainDb")
        }

        AppMenuItem {
            visible: trackData.trackKind === "audio"
            text: root.automationLaneExpanded && root.automationLaneParameter === "audioPan"
                  ? "Hide Pan Lane" : "Show Pan Lane"
            iconName: "curve"
            accent: theme.accentCool400
            onTriggered: root.automationLaneRequested("audioPan")
        }
    }

    Popup {
        id: deleteTrackConfirmation
        objectName: "timelineDeleteTrackConfirmation"
        width: Math.min(360, Math.max(300, root.width * 2))
        height: deleteTrackContent.implicitHeight + theme.space20
        x: root.width - width
        y: Math.max(0, (root.height - height) / 2)
        padding: theme.space10
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: PopupSurface {
            accent: theme.error
        }

        contentItem: ColumnLayout {
            id: deleteTrackContent
            spacing: theme.space8

            Label {
                Layout.fillWidth: true
                text: "Delete " + trackData.displayLabel + " track pair?"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: "This removes the paired video and audio lanes and "
                      + Number(trackData.clipCount || 0) + " visible clip"
                      + (Number(trackData.clipCount || 0) === 1 ? "" : "s")
                      + ". You can undo the deletion."
                color: theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                Item { Layout.fillWidth: true }

                ActionButton {
                    text: "Cancel"
                    compact: true
                    onClicked: deleteTrackConfirmation.close()
                }

                ActionButton {
                    objectName: "timelineConfirmDeleteTrackButton"
                    text: "Delete Track"
                    compact: true
                    tone: "danger"
                    onClicked: {
                        deleteTrackConfirmation.close()
                        documentController.removeTrackRow(trackData.trackIndex, true)
                    }
                }
            }
        }
    }

    Popup {
        id: automationPopup
        width: Math.min(380, Math.max(300, root.width * 1.9))
        height: Math.min(680, automationContent.implicitHeight + theme.space20)
        x: root.width - width
        y: theme.space8
        padding: theme.space10
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: PopupSurface {
            accent: trackData.trackKind === "audio" ? theme.audioAccent : theme.accentCool500
        }

        contentItem: ScrollView {
            id: mixerScroll
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: automationContent
                width: mixerScroll.availableWidth
                spacing: theme.space8

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2

                    Label {
                        text: trackData.displayLabel + (trackData.trackKind === "audio" ? " · Mixer" : " · Opacity")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeControl
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: (trackData.automationKeyframes || []).length + " automation points"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeCaption
                    }
                }

                ActionButton {
                    showText: false
                    compact: true
                    iconName: root.hasAutomationKeyAtPlayhead(root.automationParameter) ? "keyframe-filled" : "keyframe"
                    active: root.hasAutomationKeyAtPlayhead(root.automationParameter)
                    tooltipText: root.hasAutomationKeyAtPlayhead(root.automationParameter)
                                 ? "Remove the track keyframe at the playhead."
                                 : "Add a track keyframe at the playhead."
                    onClicked: documentController.toggleTrackAutomationKeyframe(
                                   trackData.trackIndex, root.automationParameter)
                }
            }

            InspectorSlider {
                Layout.fillWidth: true
                label: trackData.trackKind === "audio" ? "Gain" : "Opacity"
                valueText: trackData.trackKind === "audio"
                           ? Number(root.automationValue).toFixed(1) + " dB"
                           : Math.round(root.automationValue * 100) + "%"
                from: trackData.trackKind === "audio" ? -36 : 0
                to: trackData.trackKind === "audio" ? 0 : 1
                stepSize: trackData.trackKind === "audio" ? 0.5 : 0.01
                value: root.automationValue
                defaultValue: trackData.trackKind === "audio" ? 0 : 1
                valueInputMode: trackData.trackKind === "audio" ? "raw" : "percentNormalized"
                onInteractionStarted: documentController.beginContinuousEdit()
                onValueDragged: function(nextValue) {
                    documentController.setTrackAutomationValue(
                                trackData.trackIndex, root.automationParameter, nextValue)
                }
                onInteractionFinished: documentController.endContinuousEdit()
                onResetRequested: documentController.setTrackAutomationValue(
                                      trackData.trackIndex,
                                      root.automationParameter,
                                      trackData.trackKind === "audio" ? 0 : 1)
            }

            RowLayout {
                visible: trackData.trackKind === "audio"
                Layout.fillWidth: true
                spacing: theme.space6

                InspectorSlider {
                    Layout.fillWidth: true
                    label: "Pan"
                    valueText: Math.abs(root.panValue) < 0.005
                               ? "Center"
                               : (root.panValue < 0
                                  ? "L " + Math.round(Math.abs(root.panValue) * 100)
                                  : "R " + Math.round(root.panValue * 100))
                    from: -1
                    to: 1
                    stepSize: 0.01
                    value: root.panValue
                    defaultValue: 0
                    valueInputMode: "raw"
                    onInteractionStarted: documentController.beginContinuousEdit()
                    onValueDragged: function(nextValue) {
                        documentController.setTrackAutomationValue(
                                    trackData.trackIndex, "audioPan", nextValue)
                    }
                    onInteractionFinished: documentController.endContinuousEdit()
                    onResetRequested: documentController.setTrackAutomationValue(
                                          trackData.trackIndex, "audioPan", 0)
                }

                ActionButton {
                    showText: false
                    compact: true
                    iconName: root.hasAutomationKeyAtPlayhead("audioPan") ? "keyframe-filled" : "keyframe"
                    active: root.hasAutomationKeyAtPlayhead("audioPan")
                    tooltipText: root.hasAutomationKeyAtPlayhead("audioPan")
                                 ? "Remove the pan keyframe at the playhead."
                                 : "Add a pan keyframe at the playhead."
                    onClicked: documentController.toggleTrackAutomationKeyframe(
                                   trackData.trackIndex, "audioPan")
                }
            }

            InspectorSection {
                visible: trackData.trackKind === "audio"
                Layout.fillWidth: true
                title: "Processing"
                subtitle: "Bus routing, tone, dynamics, and dialogue priority"
                accent: theme.accentWarm500
                expandable: true
                expanded: false
                changed: String(trackData.audioBus || "main") !== "main"
                         || Math.abs(Number(trackData.audioEqLowDb || 0)) > 0.01
                         || Math.abs(Number(trackData.audioEqMidDb || 0)) > 0.01
                         || Math.abs(Number(trackData.audioEqHighDb || 0)) > 0.01
                         || trackData.audioCompressorEnabled === true
                         || trackData.audioDuckingEnabled === true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    Label {
                        text: "Route"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        Layout.fillWidth: true
                    }

                    ToneComboBox {
                        id: audioBusSelector
                        Layout.preferredWidth: 132
                        model: ["Main", "Dialogue", "Music", "SFX"]
                        currentIndex: root.audioBusIndex(trackData.audioBus)
                        accent: theme.accentWarm500
                        onActivated: documentController.setTrackAudioSetting(
                                         trackData.trackIndex,
                                         "audioBus",
                                         String(currentText).toLowerCase())
                    }
                }

                Label {
                    text: "THREE-BAND EQ"
                    color: theme.textFaint
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeCaption
                    font.weight: Font.DemiBold
                }

                InspectorSlider {
                    Layout.fillWidth: true
                    label: "Low · 120 Hz"
                    valueText: Number(trackData.audioEqLowDb || 0).toFixed(1) + " dB"
                    from: -12; to: 12; stepSize: 0.5
                    value: Number(trackData.audioEqLowDb || 0)
                    defaultValue: 0
                    valueInputMode: "raw"
                    onInteractionStarted: documentController.beginContinuousEdit()
                    onValueDragged: function(nextValue) { documentController.setTrackAudioSetting(trackData.trackIndex, "audioEqLowDb", nextValue) }
                    onInteractionFinished: documentController.endContinuousEdit()
                    onResetRequested: documentController.setTrackAudioSetting(trackData.trackIndex, "audioEqLowDb", 0)
                }

                InspectorSlider {
                    Layout.fillWidth: true
                    label: "Mid · 1 kHz"
                    valueText: Number(trackData.audioEqMidDb || 0).toFixed(1) + " dB"
                    from: -12; to: 12; stepSize: 0.5
                    value: Number(trackData.audioEqMidDb || 0)
                    defaultValue: 0
                    valueInputMode: "raw"
                    onInteractionStarted: documentController.beginContinuousEdit()
                    onValueDragged: function(nextValue) { documentController.setTrackAudioSetting(trackData.trackIndex, "audioEqMidDb", nextValue) }
                    onInteractionFinished: documentController.endContinuousEdit()
                    onResetRequested: documentController.setTrackAudioSetting(trackData.trackIndex, "audioEqMidDb", 0)
                }

                InspectorSlider {
                    Layout.fillWidth: true
                    label: "High · 8 kHz"
                    valueText: Number(trackData.audioEqHighDb || 0).toFixed(1) + " dB"
                    from: -12; to: 12; stepSize: 0.5
                    value: Number(trackData.audioEqHighDb || 0)
                    defaultValue: 0
                    valueInputMode: "raw"
                    onInteractionStarted: documentController.beginContinuousEdit()
                    onValueDragged: function(nextValue) { documentController.setTrackAudioSetting(trackData.trackIndex, "audioEqHighDb", nextValue) }
                    onInteractionFinished: documentController.endContinuousEdit()
                    onResetRequested: documentController.setTrackAudioSetting(trackData.trackIndex, "audioEqHighDb", 0)
                }

                ActionButton {
                    Layout.fillWidth: true
                    text: trackData.audioCompressorEnabled === true ? "Compressor on" : "Enable compressor"
                    iconName: "curve"
                    compact: true
                    active: trackData.audioCompressorEnabled === true
                    accent: theme.accentWarm500
                    onClicked: documentController.setTrackAudioSetting(
                                   trackData.trackIndex,
                                   "audioCompressorEnabled",
                                   trackData.audioCompressorEnabled !== true)
                }

                InspectorSlider {
                    visible: trackData.audioCompressorEnabled === true
                    Layout.fillWidth: true
                    label: "Threshold"
                    valueText: Number(trackData.audioCompressorThresholdDb !== undefined ? trackData.audioCompressorThresholdDb : -18).toFixed(0) + " dB"
                    from: -48; to: -1; stepSize: 1
                    value: Number(trackData.audioCompressorThresholdDb !== undefined ? trackData.audioCompressorThresholdDb : -18)
                    defaultValue: -18
                    valueInputMode: "raw"
                    onInteractionStarted: documentController.beginContinuousEdit()
                    onValueDragged: function(nextValue) { documentController.setTrackAudioSetting(trackData.trackIndex, "audioCompressorThresholdDb", nextValue) }
                    onInteractionFinished: documentController.endContinuousEdit()
                    onResetRequested: documentController.setTrackAudioSetting(trackData.trackIndex, "audioCompressorThresholdDb", -18)
                }

                InspectorSlider {
                    visible: trackData.audioCompressorEnabled === true
                    Layout.fillWidth: true
                    label: "Ratio"
                    valueText: Number(trackData.audioCompressorRatio !== undefined ? trackData.audioCompressorRatio : 4).toFixed(1) + ":1"
                    from: 1; to: 20; stepSize: 0.5
                    value: Number(trackData.audioCompressorRatio !== undefined ? trackData.audioCompressorRatio : 4)
                    defaultValue: 4
                    valueInputMode: "raw"
                    onInteractionStarted: documentController.beginContinuousEdit()
                    onValueDragged: function(nextValue) { documentController.setTrackAudioSetting(trackData.trackIndex, "audioCompressorRatio", nextValue) }
                    onInteractionFinished: documentController.endContinuousEdit()
                    onResetRequested: documentController.setTrackAudioSetting(trackData.trackIndex, "audioCompressorRatio", 4)
                }

                InspectorSlider {
                    visible: trackData.audioCompressorEnabled === true
                    Layout.fillWidth: true
                    label: "Makeup"
                    valueText: "+" + Number(trackData.audioCompressorMakeupDb || 0).toFixed(1) + " dB"
                    from: 0; to: 18; stepSize: 0.5
                    value: Number(trackData.audioCompressorMakeupDb || 0)
                    defaultValue: 0
                    valueInputMode: "raw"
                    onInteractionStarted: documentController.beginContinuousEdit()
                    onValueDragged: function(nextValue) { documentController.setTrackAudioSetting(trackData.trackIndex, "audioCompressorMakeupDb", nextValue) }
                    onInteractionFinished: documentController.endContinuousEdit()
                    onResetRequested: documentController.setTrackAudioSetting(trackData.trackIndex, "audioCompressorMakeupDb", 0)
                }

                ActionButton {
                    visible: String(trackData.audioBus || "main") !== "dialogue"
                    Layout.fillWidth: true
                    text: trackData.audioDuckingEnabled === true ? "Dialogue ducking on" : "Duck under dialogue"
                    iconName: "audio"
                    compact: true
                    active: trackData.audioDuckingEnabled === true
                    accent: theme.accentCool500
                    tooltipText: "Automatically lower this track whenever the Dialogue bus is active."
                    onClicked: documentController.setTrackAudioSetting(
                                   trackData.trackIndex,
                                   "audioDuckingEnabled",
                                   trackData.audioDuckingEnabled !== true)
                }

                InspectorSlider {
                    visible: trackData.audioDuckingEnabled === true
                             && String(trackData.audioBus || "main") !== "dialogue"
                    Layout.fillWidth: true
                    label: "Duck amount"
                    valueText: Number(trackData.audioDuckingAmountDb !== undefined ? trackData.audioDuckingAmountDb : -12).toFixed(0) + " dB"
                    from: -30; to: 0; stepSize: 1
                    value: Number(trackData.audioDuckingAmountDb !== undefined ? trackData.audioDuckingAmountDb : -12)
                    defaultValue: -12
                    valueInputMode: "raw"
                    onInteractionStarted: documentController.beginContinuousEdit()
                    onValueDragged: function(nextValue) { documentController.setTrackAudioSetting(trackData.trackIndex, "audioDuckingAmountDb", nextValue) }
                    onInteractionFinished: documentController.endContinuousEdit()
                    onResetRequested: documentController.setTrackAudioSetting(trackData.trackIndex, "audioDuckingAmountDb", -12)
                }
            }

                ActionButton {
                    Layout.alignment: Qt.AlignRight
                    text: "Clear automation"
                    compact: true
                    enabled: (trackData.automationKeyframes || []).length > 0
                    tooltipText: "Remove every automation point from this track."
                    onClicked: documentController.clearTrackAutomation(
                                   trackData.trackIndex,
                                   trackData.trackKind === "audio" ? "" : root.automationParameter)
                }
            }
        }
    }
}
