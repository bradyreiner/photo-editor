pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    required property var theme
    required property var trackData
    required property var timelineDocument
    required property var trackOrderData
    property bool meterVisible: false
    property real meterLeft: 0
    property real meterRight: 0
    property bool meterClipping: false
    property bool automationLaneExpanded: false
    property string automationLaneParameter: trackData.trackKind === "audio" ? "audioGainDb" : "opacity"
    property int groupTransformScaleDraft: 100
    property int groupTransformOffsetXDraft: 0
    property int groupTransformOffsetYDraft: 0
    property int groupTransformRotationDraft: 0
    property int reorderPreviewDirection: 0
    property bool reorderHandlePressed: false
    property var trackMenuInstance: null

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
                                               && !(trackData.trackKind === "video"
                                                    && String(trackData.trackRole || "media") !== "media")
    readonly property bool canTogglePrimary: trackData.trackKind === "video" ? canToggleVisibility : canToggleMuted
    readonly property bool isMediaTrack: String(trackData.trackRole || "media") !== "caption"
    readonly property bool isIndependentVideo: trackData.isIndependentVideo === true
    readonly property int groupDepth: Number(trackOrderData.groupDepth || 0)
    readonly property int pairedLaneIndex: Number(trackData.laneIndex || 0)
    readonly property int standaloneVisualPosition: Number(
        trackOrderData.standaloneVisualPosition !== undefined
        ? trackOrderData.standaloneVisualPosition : -1)
    readonly property int standaloneVisualCount: Number(trackOrderData.standaloneVisualCount || 0)
    readonly property int independentAudioPosition: Number(
        trackOrderData.independentAudioPosition !== undefined
        ? trackOrderData.independentAudioPosition : -1)
    readonly property int independentAudioCount: Number(trackOrderData.independentAudioCount || 0)
    readonly property bool isGroupHead: trackOrderData.isGroupHead === true
    readonly property string groupHeadingText: {
        const trackLabel = String(trackData.displayLabel || "Track")
        const groupName = String(trackData.groupName || "")
        return root.isGroupHead && groupName.length > 0
                ? trackLabel + " · " + groupName
                : trackLabel
    }
    readonly property var trackGroupData: {
        const groups = documentController ? documentController.trackGroups : []
        const groupId = String(trackData.groupId || "")
        for (let index = 0; index < groups.length; ++index) {
            if (String(groups[index].groupId || "") === groupId)
                return groups[index]
        }
        return ({})
    }
    readonly property bool canMoveEarlier: isMediaTrack && (trackData.isStandaloneVisual === true
                                                ? standaloneVisualPosition > 0
                                                : trackData.trackKind === "audio" && trackData.isIndependent === true
                                                  ? independentAudioPosition > 0
                                                  : pairedLaneIndex > 0)
    readonly property bool canMoveLater: isMediaTrack && documentController !== null
                                               && (trackData.isStandaloneVisual === true
                                                   ? standaloneVisualPosition >= 0
                                                     && standaloneVisualPosition < standaloneVisualCount - 1
                                                   : trackData.trackKind === "audio" && trackData.isIndependent === true
                                                   ? independentAudioPosition >= 0
                                                     && independentAudioPosition < independentAudioCount - 1
                                                   : pairedLaneIndex < Number(documentController.videoTrackCount) - 1)
    readonly property bool canDeleteTrack: isMediaTrack
                                           && documentController !== null
                                           && (trackData.isStandaloneVisual === true
                                               || trackData.trackKind === "audio" && trackData.isIndependent === true
                                               || Number(documentController.videoTrackCount) > 1)
    readonly property var nestableTrackGroups: {
        const groups = documentController ? documentController.trackGroups : []
        const currentGroupId = String(trackData.groupId || "")
        const groupsById = ({})
        const candidates = []
        for (let index = 0; index < groups.length; ++index)
            groupsById[String(groups[index].groupId || "")] = groups[index]
        for (let index = 0; index < groups.length; ++index) {
            const candidate = groups[index]
            const candidateId = String(candidate.groupId || "")
            if (candidateId.length === 0 || candidateId === currentGroupId)
                continue
            let ancestorId = String(candidate.parentGroupId || "")
            let descendant = false
            const visited = ({})
            while (ancestorId.length > 0 && visited[ancestorId] !== true) {
                if (ancestorId === currentGroupId) {
                    descendant = true
                    break
                }
                visited[ancestorId] = true
                const ancestor = groupsById[ancestorId]
                ancestorId = String(ancestor ? ancestor.parentGroupId || "" : "")
            }
            if (!descendant)
                candidates.push(candidate)
        }
        return candidates
    }

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

    function commitTrackRename() {
        if (documentController)
            documentController.renameTrackRow(trackData.trackIndex, trackNameField.text)
        trackRenamePopup.close()
    }

    function openTrackMenu() {
        const menu = trackMenuComponent.createObject(root)
        if (!menu)
            return false
        trackMenuInstance = menu
        menu.popup()
        return true
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
        if (parameterName === "audioEqLowDb") return "EQ Low"
        if (parameterName === "audioEqMidDb") return "EQ Mid"
        if (parameterName === "audioEqHighDb") return "EQ High"
        if (parameterName === "audioCompressorThresholdDb") return "Compressor Threshold"
        if (parameterName === "audioCompressorRatio") return "Compressor Ratio"
        if (parameterName === "audioCompressorMakeupDb") return "Compressor Makeup"
        if (parameterName === "audioLimiterCeilingDb") return "Limiter Ceiling"
        if (parameterName === "audioGateThresholdDb") return "Gate Threshold"
        if (parameterName === "audioDeEsserAmount") return "De-Esser"
        if (parameterName === "audioHighPassHz") return "High-Pass Frequency"
        if (parameterName === "audioLowPassHz") return "Low-Pass Frequency"
        return "Opacity"
    }

    readonly property string timelineTrackHeaderStyleContract: "timeline-track-header-style:v3|surface:quiet-editorial|padding:compact|type:shared|targets:32|states:patched-locked-muted-solo-hidden|patch:independent-exclusive-stream-destination|target:accent-rail|edges:structural-only|resize:intrinsic"
    readonly property string timelineTrackMixerContract: "timeline-track-mixer:v3|popup:gain-pan-processing|automation:inline-lanes-contextual|routing:main-dialogue-music-sfx|processing:progressive-three-band-eq-compressor-ducking|meters:contextual-stereo-peak-clip|default-density:unchanged|controls:shared"

    radius: 0
    color: "transparent"
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
        anchors.leftMargin: (root.height < theme.scaled(56) ? theme.space4 : theme.space6)
                            + Math.min(root.groupDepth, 4) * theme.scaled(12)
        spacing: theme.space4

        RowLayout {
            visible: root.height >= theme.scaled(56)
            Layout.fillWidth: true
            spacing: theme.space6

            Label {
                Layout.fillWidth: true
                text: root.groupHeadingText
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: theme.typeLabel
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Button {
                visible: root.isGroupHead && root.height >= theme.scaled(56)
                Layout.preferredWidth: visible ? theme.scaled(18) : 0
                Layout.preferredHeight: visible ? theme.scaled(18) : 0
                padding: 0
                flat: true
                contentItem: AppIcon {
                    name: trackData.groupCollapsed === true ? "chevron-right" : "chevron-down"
                    color: theme.textMuted
                    width: theme.scaled(14)
                    height: width
                }
                Accessible.name: trackData.groupCollapsed === true
                                 ? "Expand track group " + String(trackData.groupName || "")
                                 : "Collapse track group " + String(trackData.groupName || "")
                onClicked: documentController.setTrackGroupCollapsed(
                               String(trackData.groupId || ""), trackData.groupCollapsed !== true)
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
                visible: !root.isGroupHead || String(trackData.groupName || "").length === 0
                text: trackData.trackKind === "audio" ? "Audio"
                      : trackData.isStandaloneVisual === true ? "Graphics"
                      : trackData.isCaptionTrack === true ? "Captions" : "Video"
                color: trackData.targeted ? root.patchAccent : theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
            }

            StatusChip {
                objectName: "timelineTrackLockedStateChip"
                visible: trackData.locked === true
                text: "LOCKED"
                iconName: "lock"
                tone: "warning"
                compact: true
                minimumReadableWidth: 72
                maximumReadableWidth: 82
                tooltipText: "This track is locked. Unlock it before moving or editing its clips."
            }

            Item {
                id: trackReorderHandle
                objectName: "timelineTrackReorderHandle"
                Layout.preferredWidth: theme.space16
                Layout.preferredHeight: theme.space16
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Reorder " + trackData.displayLabel
                             + (trackData.isStandaloneVisual === true ? " graphics track"
                                : root.isIndependentVideo ? " video lane"
                                : trackData.trackKind === "audio" && trackData.isIndependent === true
                                  ? " audio track" : " track pair")

                Rectangle {
                    anchors.fill: parent
                    radius: theme.radius4
                    color: root.reorderHandlePressed
                           ? theme.alpha(root.patchAccent, 0.18)
                           : trackReorderMouse.containsMouse || trackReorderHandle.activeFocus
                             ? theme.alpha(theme.editorControlHover, 0.66) : "transparent"
                }

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
                                   || root.reorderHandlePressed
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
                        root.reorderHandlePressed = true
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
                        root.reorderHandlePressed = false
                        if (direction !== 0)
                            documentController.moveTrackRow(trackData.trackIndex, direction)
                    }
                    onCanceled: {
                        root.reorderPreviewDirection = 0
                        root.reorderHandlePressed = false
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space2

            Components.ToolButton {
                implicitWidth: theme.controlHeightDense
                implicitHeight: theme.controlHeightDense
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: theme.controlHeightDense
                label: trackData.trackKind === "video"
                       ? (trackData.hiddenByGroup === true
                          ? "Hidden by Group · show the group to change track visibility"
                          : (trackData.visible ? "Hide Video Track" : "Show Video Track"))
                       : (trackData.muted ? "Unmute Audio Track" : "Mute Audio Track")
                iconName: trackData.trackKind === "video"
                          ? (trackData.effectiveVisible === true ? "visible" : "hidden")
                          : (trackData.muted ? "audio-muted" : "audio")
                active: trackData.trackKind === "video"
                        ? trackData.effectiveVisible === true : trackData.muted
                accent: trackData.trackKind === "video" ? theme.accentCool500 : theme.error
                Accessible.role: Accessible.Button
                Accessible.checked: trackData.trackKind === "video"
                                    ? trackData.effectiveVisible === true : trackData.muted
                enabled: canTogglePrimary
                         && !(trackData.trackKind === "video"
                              && trackData.hiddenByGroup === true)
                onClicked: root.togglePrimaryTrackState()
            }

            Components.ToolButton {
                implicitWidth: theme.controlHeightDense
                implicitHeight: theme.controlHeightDense
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: theme.controlHeightDense
                visible: trackData.trackKind === "audio"
                label: trackData.solo ? "Clear Solo" : "Solo Track"
                iconName: "solo"
                active: trackData.solo
                accent: theme.audioAccent
                Accessible.role: Accessible.Button
                Accessible.checked: trackData.solo
                enabled: canToggleSolo
                onClicked: root.toggleSoloTrackState()
            }

            Components.ToolButton {
                implicitWidth: theme.controlHeightDense
                implicitHeight: theme.controlHeightDense
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: theme.controlHeightDense
                visible: canToggleTargeted
                label: trackData.targeted
                       ? "Disable source " + trackData.trackKind + " patch to " + trackData.displayLabel
                       : "Patch source " + trackData.trackKind + " to " + trackData.displayLabel
                iconName: "source"
                active: trackData.targeted
                accent: root.patchAccent
                Accessible.role: Accessible.Button
                Accessible.checked: trackData.targeted
                enabled: canToggleTargeted
                onClicked: root.toggleTargetedTrackState()
            }

            Components.ToolButton {
                implicitWidth: theme.controlHeightDense
                implicitHeight: theme.controlHeightDense
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.maximumWidth: theme.controlHeightDense
                label: trackData.locked ? "Unlock Track" : "Lock Track"
                iconName: trackData.locked ? "lock" : "unlock"
                active: trackData.locked
                accent: theme.warning
                Accessible.role: Accessible.Button
                Accessible.checked: trackData.locked
                enabled: canToggleLocked
                onClicked: root.toggleLockedTrackState()
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
                root.openTrackMenu()
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
        height: theme.scaled(10)
        color: "transparent"
        activeFocusOnTab: true
        Accessible.role: Accessible.Slider
        Accessible.name: "Resize " + String(trackData.displayLabel || "track") + " height"
        Accessible.description: "Use Up and Down Arrow to resize this track."
        Keys.onUpPressed: root.resizeRequested(-8)
        Keys.onDownPressed: root.resizeRequested(8)

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: theme.scaled(2)
            radius: height / 2
            color: resizeHandle.containsMouse || resizeHandle.pressed || trackHeightResizeHandle.activeFocus
                   ? theme.focusRing : "transparent"
        }

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

    Component {
        id: trackMenuComponent

        AppMenu {
            id: trackMenu
            objectName: "timelineTrackContextMenu"
            accent: trackData.trackKind === "video" ? theme.accentCool500 : theme.audioAccent
            onClosed: {
                if (root.trackMenuInstance === trackMenu)
                    root.trackMenuInstance = null
                destroy()
            }

            AppMenuItem {
                objectName: "timelineRenameTrackAction"
                text: "Rename Track…"
                iconName: "edit"
                accent: root.patchAccent
                onTriggered: {
                    trackRenamePopup.open()
                }
            }

            AppMenuItem {
                objectName: "timelineDuplicateTrackAction"
                visible: trackData.isCaptionTrack !== true
                text: "Duplicate Track"
                iconName: "copy"
                accent: root.patchAccent
                onTriggered: root.documentController.duplicateTrackRow(root.trackData.trackIndex)
            }

            AppMenuItem {
                text: "Create Track Group…"
                iconName: "plus"
                accent: root.patchAccent
                onTriggered: createTrackGroupPopup.open()
            }

            AppMenuItem {
                visible: root.documentController !== null
                         && root.documentController.trackGroups.length > 0
                         && String(root.trackData.groupId || "").length === 0
                text: "Add to Track Group…"
                iconName: "layers"
                accent: root.patchAccent
                onTriggered: addToTrackGroupPopup.open()
            }

            AppMenuItem {
                visible: root.isGroupHead && String(root.trackData.groupId || "").length > 0
                text: "Rename Group…"
                iconName: "edit"
                accent: root.patchAccent
                onTriggered: renameTrackGroupPopup.open()
            }

            AppMenuItem {
                objectName: "timelineNestTrackGroupAction"
                visible: root.isGroupHead && String(root.trackData.groupId || "").length > 0
                text: "Nest Group Under…"
                iconName: "layers"
                accent: root.patchAccent
                onTriggered: nestTrackGroupPopup.open()
            }

            AppMenuItem {
                objectName: "timelineToggleTrackGroupCollapseAction"
                visible: root.isGroupHead && String(root.trackData.groupId || "").length > 0
                text: root.trackData.groupCollapsed === true
                      ? "Expand Track Group" : "Collapse Track Group"
                iconName: root.trackData.groupCollapsed === true ? "plus" : "minus"
                accent: root.patchAccent
                onTriggered: root.documentController.setTrackGroupCollapsed(
                                 String(root.trackData.groupId || ""),
                                 root.trackData.groupCollapsed !== true)
            }

            AppMenuItem {
                objectName: "timelineSelectTrackGroupClipsAction"
                visible: root.isGroupHead && String(root.trackData.groupId || "").length > 0
                text: "Select Group Clips"
                iconName: "select"
                accent: root.patchAccent
                enabled: Number(root.trackGroupData.subtreeTrackCount || 0) > 0
                tooltipText: "Select clips across this group and its nested groups."
                onTriggered: root.documentController.selectTrackGroupClips(
                                 String(root.trackData.groupId || ""))
            }

            AppMenuItem {
                objectName: "timelineEditTrackGroupTransformAction"
                visible: root.isGroupHead
                         && Number(root.trackGroupData.subtreeVideoTrackCount || 0) > 0
                text: "Group Transform…"
                iconName: "crop"
                accent: root.patchAccent
                onTriggered: groupTransformPopup.open()
            }

            AppMenuItem {
                objectName: "timelineToggleTrackGroupLockAction"
                visible: root.isGroupHead
                         && Number(root.trackGroupData.subtreeTrackCount || 0) > 0
                text: root.trackGroupData.allTracksLocked === true
                      ? "Unlock Group Tracks" : "Lock Group Tracks"
                iconName: root.trackGroupData.allTracksLocked === true ? "unlock" : "lock"
                accent: root.patchAccent
                onTriggered: root.documentController.setTrackGroupState(
                                 String(root.trackData.groupId || ""), "locked",
                                 root.trackGroupData.allTracksLocked !== true)
            }

            AppMenuItem {
                objectName: "timelineToggleTrackGroupMuteAction"
                visible: root.isGroupHead
                         && Number(root.trackGroupData.subtreeAudioTrackCount || 0) > 0
                text: root.trackGroupData.allAudioMuted === true
                      ? "Unmute Group Audio" : "Mute Group Audio"
                iconName: "audio"
                accent: root.patchAccent
                onTriggered: root.documentController.setTrackGroupState(
                                 String(root.trackData.groupId || ""), "muted",
                                 root.trackGroupData.allAudioMuted !== true)
            }

            AppMenuItem {
                objectName: "timelineToggleTrackGroupVisibilityAction"
                visible: root.isGroupHead
                         && Number(root.trackGroupData.subtreeVideoTrackCount || 0) > 0
                text: root.trackGroupData.videoHidden === true
                      ? "Show Group Video" : "Hide Group Video"
                iconName: root.trackGroupData.videoHidden === true ? "visible" : "hidden"
                accent: root.patchAccent
                onTriggered: root.documentController.setTrackGroupState(
                                 String(root.trackData.groupId || ""), "visible",
                                 root.trackGroupData.videoHidden === true)
            }

            AppMenuItem {
                visible: String(root.trackData.groupId || "").length > 0
                text: "Remove from Group"
                iconName: "minus"
                accent: root.patchAccent
                onTriggered: root.documentController.setTrackGroup(root.trackData.trackIndex, "")
            }

            AppMenuItem {
                text: trackData.locked ? "Unlock Track" : "Lock Track"
                iconName: trackData.locked ? "unlock" : "lock"
                accent: root.patchAccent
                enabled: canToggleLocked
                onTriggered: root.toggleLockedTrackState()
            }

            AppMenuItem {
                text: trackData.targeted
                      ? "Disable Source Patch"
                      : "Patch Source to " + trackData.displayLabel
                iconName: "check"
                accent: root.patchAccent
                enabled: canToggleTargeted
                visible: canToggleTargeted
                onTriggered: root.toggleTargetedTrackState()
            }

            AppMenuItem {
                visible: trackData.trackKind === "video"
                text: trackData.hiddenByGroup === true
                      ? "Hidden by Group"
                      : (trackData.visible ? "Hide Video Track" : "Show Video Track")
                iconName: trackData.visible ? "hidden" : "visible"
                accent: root.patchAccent
                enabled: canToggleVisibility && trackData.hiddenByGroup !== true
                onTriggered: root.togglePrimaryTrackState()
            }

            AppMenuItem {
                visible: trackData.trackKind === "audio"
                text: trackData.muted ? "Unmute Audio Track" : "Mute Audio Track"
                iconName: "audio"
                accent: root.patchAccent
                enabled: canToggleMuted
                onTriggered: root.togglePrimaryTrackState()
            }

            AppMenuItem {
                visible: trackData.trackKind === "audio"
                text: trackData.solo ? "Clear Solo" : "Solo Track"
                iconName: "solo"
                accent: root.patchAccent
                enabled: canToggleSolo
                onTriggered: root.toggleSoloTrackState()
            }

            MenuSeparator {}

            AppMenuItem {
                objectName: "timelineMoveTrackEarlierAction"
                text: "Move Track Up"
                shortcutText: "⌥↑"
                accent: root.patchAccent
                enabled: root.canMoveEarlier
                onTriggered: documentController.moveTrackRow(trackData.trackIndex, -1)
            }

            AppMenuItem {
                objectName: "timelineMoveTrackLaterAction"
                text: "Move Track Down"
                shortcutText: "⌥↓"
                accent: root.patchAccent
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
                accent: root.patchAccent
                onTriggered: root.heightPresetRequested(40)
            }

            AppMenuItem {
                text: "Standard Track Height"
                accent: root.patchAccent
                onTriggered: root.heightPresetRequested(62)
            }

            AppMenuItem {
                text: "Large Track Height"
                accent: root.patchAccent
                onTriggered: root.heightPresetRequested(96)
            }

            AppMenuItem {
                text: trackData.trackKind === "audio" ? "Track mixer…" : "Track opacity automation…"
                iconName: "curve"
                accent: root.patchAccent
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

            AppMenu {
                title: "Processor Automation"
                accent: theme.audioAccent
                enabled: trackData.trackKind === "audio"

                AppMenu {
                    title: "Equalizer"
                    accent: theme.audioAccent

                    AppMenuItem {
                        text: "Low Band"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioEqLowDb")
                    }

                    AppMenuItem {
                        text: "Mid Band"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioEqMidDb")
                    }

                    AppMenuItem {
                        text: "High Band"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioEqHighDb")
                    }
                }

                AppMenu {
                    title: "Dynamics"
                    accent: theme.audioAccent

                    AppMenuItem {
                        text: "Compressor Threshold"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioCompressorThresholdDb")
                    }

                    AppMenuItem {
                        text: "Compressor Ratio"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioCompressorRatio")
                    }

                    AppMenuItem {
                        text: "Compressor Makeup"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioCompressorMakeupDb")
                    }

                    AppMenuItem {
                        text: "Limiter Ceiling"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioLimiterCeilingDb")
                    }

                    AppMenuItem {
                        text: "Gate Threshold"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioGateThresholdDb")
                    }

                    AppMenuItem {
                        text: "De-Esser Amount"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioDeEsserAmount")
                    }
                }

                AppMenu {
                    title: "Filters"
                    accent: theme.audioAccent

                    AppMenuItem {
                        text: "High-Pass Frequency"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioHighPassHz")
                    }

                    AppMenuItem {
                        text: "Low-Pass Frequency"
                        iconName: "curve"
                        accent: theme.audioAccent
                        onTriggered: root.automationLaneRequested("audioLowPassHz")
                    }
                }
            }
        }
    }

    AppPopup {
        id: groupTransformPopup
        objectName: "timelineTrackGroupTransformPopup"
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(340, Math.max(280, root.width * 1.9))))
        height: Math.min(popupAvailableHeight, Math.max(1, groupTransformContent.implicitHeight + root.theme.space20))
        x: root.width - width
        y: Math.max(0, (root.height - height) / 2)
        padding: standardPopupPadding
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            root.groupTransformScaleDraft = Math.round(
                        Number(root.trackGroupData.transformScale || 1) * 100)
            root.groupTransformOffsetXDraft = Math.round(
                        Number(root.trackGroupData.transformOffsetX || 0) * 100)
            root.groupTransformOffsetYDraft = Math.round(
                        Number(root.trackGroupData.transformOffsetY || 0) * 100)
            root.groupTransformRotationDraft = Math.round(
                        Number(root.trackGroupData.transformRotationDegrees || 0))
        }
        background: PopupSurface {}
        contentItem: ColumnLayout {
            id: groupTransformContent
            spacing: root.theme.space8

            Label {
                Layout.fillWidth: true
                text: "Transform “" + String(root.trackData.groupName || "Track Group") + "”"
                color: root.theme.textStrong
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeControl
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: "Applies to every visual track in this group and its nested groups."
                color: root.theme.textMuted
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeCaption
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: "Scale"
                    color: root.theme.textMain
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeControl
                }
                ToneSpinBox {
                    objectName: "timelineGroupTransformScale"
                    from: 25
                    to: 200
                    stepSize: 1
                    editable: true
                    value: root.groupTransformScaleDraft
                    valueUnit: "%"
                    accessibleLabel: "Group scale percent"
                    onValueChanged: root.groupTransformScaleDraft = value
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: "Horizontal position"
                    color: root.theme.textMain
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeControl
                }
                ToneSpinBox {
                    objectName: "timelineGroupTransformOffsetX"
                    from: -50
                    to: 50
                    stepSize: 1
                    editable: true
                    value: root.groupTransformOffsetXDraft
                    valueUnit: "% frame"
                    accessibleLabel: "Group horizontal position percent"
                    onValueChanged: root.groupTransformOffsetXDraft = value
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: "Vertical position"
                    color: root.theme.textMain
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeControl
                }
                ToneSpinBox {
                    objectName: "timelineGroupTransformOffsetY"
                    from: -50
                    to: 50
                    stepSize: 1
                    editable: true
                    value: root.groupTransformOffsetYDraft
                    valueUnit: "% frame"
                    accessibleLabel: "Group vertical position percent"
                    onValueChanged: root.groupTransformOffsetYDraft = value
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: "Rotation"
                    color: root.theme.textMain
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeControl
                }
                ToneSpinBox {
                    objectName: "timelineGroupTransformRotation"
                    from: -180
                    to: 180
                    stepSize: 1
                    editable: true
                    value: root.groupTransformRotationDraft
                    valueUnit: "°"
                    accessibleLabel: "Group rotation degrees"
                    onValueChanged: root.groupTransformRotationDraft = value
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: root.theme.space6
                ActionButton {
                    text: "Reset"
                    compact: true
                    onClicked: {
                        root.documentController.setTrackGroupTransform(
                                    String(root.trackData.groupId || ""), 1, 0, 0, 0)
                        groupTransformPopup.close()
                    }
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "Cancel"
                    compact: true
                    onClicked: groupTransformPopup.close()
                }
                ActionButton {
                    text: "Apply"
                    compact: true
                    onClicked: {
                        root.documentController.setTrackGroupTransform(
                                    String(root.trackData.groupId || ""),
                                    root.groupTransformScaleDraft / 100,
                                    root.groupTransformOffsetXDraft / 100,
                                    root.groupTransformOffsetYDraft / 100,
                                    root.groupTransformRotationDraft)
                        groupTransformPopup.close()
                    }
                }
            }
        }
    }

    AppPopup {
        id: createTrackGroupPopup
        objectName: "timelineCreateTrackGroupPopup"
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(320, Math.max(260, root.width * 1.8))))
        height: Math.min(popupAvailableHeight, Math.max(1, createTrackGroupContent.implicitHeight + root.theme.space20))
        x: root.width - width
        y: Math.max(0, (root.height - height) / 2)
        padding: standardPopupPadding
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            newTrackGroupName.text = ""
            Qt.callLater(function() { newTrackGroupName.forceActiveFocus(Qt.PopupFocusReason) })
        }
        background: PopupSurface {}
        contentItem: ColumnLayout {
            id: createTrackGroupContent
            spacing: root.theme.space8
            Label {
                Layout.fillWidth: true
                text: "Create a Track Group"
                color: root.theme.textStrong
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeControl
                font.weight: Font.DemiBold
            }
            ToneTextField {
                id: newTrackGroupName
                objectName: "timelineTrackGroupNameField"
                Layout.fillWidth: true
                accessibleLabel: "Track group name"
                placeholderText: "Enter a group name"
                maximumLength: 48
                selectByMouse: true
                onAccepted: {
                    if (root.documentController.createTrackGroup(root.trackData.trackIndex,
                                                                 newTrackGroupName.text))
                        createTrackGroupPopup.close()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "Cancel"
                    compact: true
                    onClicked: createTrackGroupPopup.close()
                }
                ActionButton {
                    text: "Create Group"
                    compact: true
                    enabled: newTrackGroupName.text.trim().length > 0
                    onClicked: {
                        if (root.documentController.createTrackGroup(root.trackData.trackIndex,
                                                                     newTrackGroupName.text))
                            createTrackGroupPopup.close()
                    }
                }
            }
        }
    }

    AppPopup {
        id: addToTrackGroupPopup
        objectName: "timelineAddToTrackGroupPopup"
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(320, Math.max(260, root.width * 1.8))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(root.theme.scaled(420),
                         groupPickerContent.implicitHeight + root.theme.space20)))
        x: root.width - width
        y: Math.max(0, (root.height - height) / 2)
        padding: standardPopupPadding
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: PopupSurface {}
        contentItem: ColumnLayout {
            id: groupPickerContent
            spacing: root.theme.space6
            Label {
                Layout.fillWidth: true
                text: "Add to Track Group"
                color: root.theme.textStrong
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeControl
                font.weight: Font.DemiBold
            }
            Repeater {
                model: root.documentController ? root.documentController.trackGroups : []
                delegate: ActionButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: String(modelData.name || "Track Group")
                    compact: true
                    tooltipText: String(modelData.trackCount || 0) + " tracks"
                    onClicked: {
                        root.documentController.setTrackGroup(root.trackData.trackIndex,
                                                              String(modelData.groupId || ""))
                        addToTrackGroupPopup.close()
                    }
                }
            }
            ActionButton {
                Layout.alignment: Qt.AlignRight
                text: "Cancel"
                compact: true
                onClicked: addToTrackGroupPopup.close()
            }
        }
    }

    AppPopup {
        id: renameTrackGroupPopup
        objectName: "timelineRenameTrackGroupPopup"
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(320, Math.max(260, root.width * 1.8))))
        height: Math.min(popupAvailableHeight, Math.max(1, renameTrackGroupContent.implicitHeight + root.theme.space20))
        x: root.width - width
        y: Math.max(0, (root.height - height) / 2)
        padding: standardPopupPadding
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            renamedTrackGroupName.text = String(root.trackData.groupName || "")
            Qt.callLater(function() { renamedTrackGroupName.forceActiveFocus(Qt.PopupFocusReason) })
        }
        background: PopupSurface {}
        contentItem: ColumnLayout {
            id: renameTrackGroupContent
            spacing: root.theme.space8
            Label {
                Layout.fillWidth: true
                text: "Rename Track Group"
                color: root.theme.textStrong
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeControl
                font.weight: Font.DemiBold
            }
            ToneTextField {
                id: renamedTrackGroupName
                Layout.fillWidth: true
                accessibleLabel: "New track group name"
                maximumLength: 48
                selectByMouse: true
                onAccepted: {
                    if (root.documentController.renameTrackGroup(
                                String(root.trackData.groupId || ""), renamedTrackGroupName.text))
                        renameTrackGroupPopup.close()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "Cancel"
                    compact: true
                    onClicked: renameTrackGroupPopup.close()
                }
                ActionButton {
                    text: "Save Name"
                    compact: true
                    enabled: renamedTrackGroupName.text.trim().length > 0
                    onClicked: {
                        if (root.documentController.renameTrackGroup(
                                    String(root.trackData.groupId || ""), renamedTrackGroupName.text))
                            renameTrackGroupPopup.close()
                    }
                }
            }
        }
    }

    AppPopup {
        id: nestTrackGroupPopup
        objectName: "timelineNestTrackGroupPopup"
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(340, Math.max(280, root.width * 1.9))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(root.theme.scaled(400), nestGroupContent.implicitHeight + root.theme.space20)))
        x: root.width - width
        y: Math.max(0, (root.height - height) / 2)
        padding: standardPopupPadding
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: PopupSurface {}
        contentItem: ColumnLayout {
            id: nestGroupContent
            spacing: root.theme.space6
            Label {
                Layout.fillWidth: true
                text: "Nest “" + String(root.trackData.groupName || "Track Group") + "” under"
                color: root.theme.textStrong
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeControl
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            ActionButton {
                visible: String(root.trackData.groupParentId || "").length > 0
                Layout.fillWidth: true
                text: "Top Level"
                compact: true
                onClicked: {
                    root.documentController.setTrackGroupParent(String(root.trackData.groupId || ""), "")
                    nestTrackGroupPopup.close()
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(root.theme.scaled(280), nestableGroupList.implicitHeight)
                clip: true
                Column {
                    id: nestableGroupList
                    width: parent.width
                    spacing: root.theme.space4
                    Repeater {
                        model: root.nestableTrackGroups
                        delegate: ActionButton {
                            required property var modelData
                            width: nestableGroupList.width
                            text: {
                                let indentation = ""
                                const depth = Math.min(5, Number(modelData.depth || 0))
                                for (let index = 0; index < depth; ++index)
                                    indentation += "  "
                                return indentation + String(modelData.name || "Track Group")
                            }
                            compact: true
                            onClicked: {
                                root.documentController.setTrackGroupParent(
                                            String(root.trackData.groupId || ""),
                                            String(modelData.groupId || ""))
                                nestTrackGroupPopup.close()
                            }
                        }
                    }
                    Label {
                        visible: root.nestableTrackGroups.length === 0
                                 && String(root.trackData.groupParentId || "").length === 0
                        width: nestableGroupList.width
                        text: "No other groups available"
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                        wrapMode: Text.WordWrap
                    }
                }
            }
            ActionButton {
                Layout.alignment: Qt.AlignRight
                text: "Cancel"
                compact: true
                onClicked: nestTrackGroupPopup.close()
            }
        }
    }

    AppPopup {
        id: trackRenamePopup
        objectName: "timelineRenameTrackPopup"
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(320, Math.max(260, root.width * 1.8))))
        height: Math.min(popupAvailableHeight, Math.max(1, renameTrackContent.implicitHeight + theme.space20))
        x: root.width - width
        y: Math.max(0, (root.height - height) / 2)
        padding: standardPopupPadding
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            trackNameField.text = String(trackData.customName || "")
            Qt.callLater(function() {
                trackNameField.forceActiveFocus(Qt.PopupFocusReason)
                trackNameField.selectAll()
            })
        }

        background: PopupSurface {}

        contentItem: ColumnLayout {
            id: renameTrackContent
            spacing: theme.space8

            Label {
                Layout.fillWidth: true
                text: "Rename " + trackData.displayLabel
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            ToneTextField {
                id: trackNameField
                objectName: "timelineTrackNameField"
                Layout.fillWidth: true
                accessibleLabel: "Track name"
                text: String(trackData.customName || "")
                placeholderText: "Enter a track name"
                maximumLength: 48
                selectByMouse: true
                onAccepted: root.commitTrackRename()
            }

            Label {
                Layout.fillWidth: true
                text: "Leave the name empty to restore the default label."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                ActionButton {
                    text: "Cancel"
                    compact: true
                    onClicked: trackRenamePopup.close()
                }

                ActionButton {
                    objectName: "timelineSaveTrackNameButton"
                    text: "Save Name"
                    compact: true
                    onClicked: root.commitTrackRename()
                }
            }
        }
    }

    AppPopup {
        id: deleteTrackConfirmation
        objectName: "timelineDeleteTrackConfirmation"
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(360, Math.max(300, root.width * 2))))
        height: Math.min(popupAvailableHeight, Math.max(1, deleteTrackContent.implicitHeight + theme.space20))
        x: root.width - width
        y: Math.max(0, (root.height - height) / 2)
        padding: standardPopupPadding
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: PopupSurface {}

        contentItem: ColumnLayout {
            id: deleteTrackContent
            spacing: theme.space8

            Label {
                Layout.fillWidth: true
                text: trackData.isStandaloneVisual === true
                      ? "Delete " + trackData.displayLabel + " graphics track?"
                      : root.isIndependentVideo
                        ? "Delete " + trackData.displayLabel + " video lane?"
                      : trackData.trackKind === "audio" && trackData.isIndependent === true
                        ? "Delete " + trackData.displayLabel + " track?"
                        : "Delete " + trackData.displayLabel + " track pair?"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: (trackData.isStandaloneVisual === true
                       ? "This removes this graphics lane and "
                       : root.isIndependentVideo
                         ? "This removes this video lane and "
                       : trackData.trackKind === "audio" && trackData.isIndependent === true
                         ? "This removes this audio lane and "
                         : "This removes the paired video and audio lanes and ")
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

    AppPopup {
        id: automationPopup
        property bool contentCreated: false
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(380, Math.max(300, root.width * 1.9))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(680,
                         (automationContentLoader.item
                          ? automationContentLoader.item.contentImplicitHeight : 0)
                         + theme.space20)))
        x: root.width - width
        y: theme.space8
        padding: theme.space10
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToShow: contentCreated = true

        background: PopupSurface {}

        contentItem: Loader {
            id: automationContentLoader
            active: automationPopup.contentCreated || automationPopup.visible
            onLoaded: automationPopup.contentCreated = true
            sourceComponent: Component {
                ScrollView {
                    id: mixerScroll
                    property real contentImplicitHeight: automationContent.implicitHeight
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
                                accessibleLabel: qsTr("Audio route bus")
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
    }
}
