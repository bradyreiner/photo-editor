import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property var trackData
    required property var timelineDocument

    signal resizeRequested(real delta)

    readonly property var documentController: timelineDocument !== undefined && timelineDocument !== null ? timelineDocument : null
    readonly property bool canToggleLocked: documentController !== null && typeof documentController.toggleTrackLocked === "function"
    readonly property bool canToggleMuted: documentController !== null && typeof documentController.toggleTrackMuted === "function"
    readonly property bool canToggleSolo: documentController !== null && typeof documentController.toggleTrackSolo === "function"
    readonly property bool canToggleVisibility: documentController !== null && typeof documentController.toggleTrackVisibility === "function"
    readonly property bool canToggleTargeted: documentController !== null && typeof documentController.toggleTrackTargeted === "function"
    readonly property bool canTogglePrimary: trackData.trackKind === "video" ? canToggleVisibility : canToggleMuted

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

    radius: 8
    color: theme.alpha(theme.ink950, 0.78)
    border.width: 1
    border.color: theme.alpha(theme.lineSoft, 0.42)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        RowLayout {
            spacing: 6

            Label {
                text: trackData.displayLabel
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            Label {
                text: trackData.trackKind === "audio" ? "A" : "V"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 9
            }
        }

        RowLayout {
            spacing: 4

            TimelineTrackToggleButton {
                label: trackData.trackKind === "video"
                       ? (trackData.visible ? "Hide Video Track" : "Show Video Track")
                       : (trackData.muted ? "Unmute Audio Track" : "Mute Audio Track")
                shortLabel: trackData.trackKind === "video" ? "V" : "M"
                iconName: trackData.trackKind === "video" ? (trackData.visible ? "visible" : "hidden") : "audio"
                checked: trackData.trackKind === "video" ? trackData.visible : trackData.muted
                checkedColor: trackData.trackKind === "video" ? theme.accentCool500 : theme.error
                enabled: canTogglePrimary
                onToggled: root.togglePrimaryTrackState()
            }

            TimelineTrackToggleButton {
                visible: trackData.trackKind === "audio"
                label: trackData.solo ? "Clear Solo" : "Solo Track"
                shortLabel: "S"
                checked: trackData.solo
                checkedColor: theme.accentWarm500
                enabled: canToggleSolo
                onToggled: root.toggleSoloTrackState()
            }

            TimelineTrackToggleButton {
                label: trackData.targeted ? "Untarget Track" : "Target Track"
                shortLabel: "T"
                iconName: "check"
                checked: trackData.targeted
                checkedColor: theme.accentWarm500
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
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 4
        color: resizeHandle.containsMouse || resizeHandle.pressed ? theme.accentWarm300 : "transparent"
        radius: 2

        MouseArea {
            id: resizeHandle
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            hoverEnabled: true
            property real startMouseY: 0

            onPressed: function(mouse) {
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
        accent: trackData.trackKind === "video" ? theme.accentCool500 : theme.accentWarm500

        AppMenuItem {
            text: trackData.locked ? "Unlock Track" : "Lock Track"
            iconName: "lock"
            accent: trackMenu.accent
            enabled: canToggleLocked
            onTriggered: root.toggleLockedTrackState()
        }

        AppMenuItem {
            text: trackData.targeted ? "Untarget Track" : "Target Track"
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
    }
}
