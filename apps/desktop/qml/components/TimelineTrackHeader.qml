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

            Rectangle {
                implicitWidth: 20
                implicitHeight: 18
                radius: 5
                color: trackData.trackKind === "video"
                       ? (trackData.visible ? theme.alpha(theme.accentCool500, 0.24) : theme.alpha(theme.ink700, 0.84))
                       : (trackData.muted ? theme.alpha(theme.error, 0.24) : theme.alpha(theme.ink700, 0.84))
                border.width: 1
                border.color: theme.alpha(theme.lineStrong, 0.42)

                Label {
                    anchors.centerIn: parent
                    text: trackData.trackKind === "video" ? "V" : "M"
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: 9
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: canTogglePrimary
                    onClicked: root.togglePrimaryTrackState()
                }
            }

            Rectangle {
                implicitWidth: 20
                implicitHeight: 18
                radius: 5
                color: trackData.solo ? theme.alpha(theme.accentWarm500, 0.24) : theme.alpha(theme.ink700, 0.84)
                border.width: 1
                border.color: theme.alpha(theme.lineStrong, 0.42)
                visible: trackData.trackKind === "audio"

                Label {
                    anchors.centerIn: parent
                    text: "S"
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: 9
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: canToggleSolo
                    onClicked: root.toggleSoloTrackState()
                }
            }

            Rectangle {
                implicitWidth: 20
                implicitHeight: 18
                radius: 5
                color: trackData.targeted ? theme.alpha(theme.accentWarm500, 0.24) : theme.alpha(theme.ink700, 0.84)
                border.width: 1
                border.color: theme.alpha(theme.lineStrong, 0.42)

                Label {
                    anchors.centerIn: parent
                    text: "T"
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: 9
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: canToggleTargeted
                    onClicked: root.toggleTargetedTrackState()
                }
            }

            Rectangle {
                implicitWidth: 20
                implicitHeight: 18
                radius: 5
                color: trackData.locked ? theme.alpha(theme.warning, 0.24) : theme.alpha(theme.ink700, 0.84)
                border.width: 1
                border.color: theme.alpha(theme.lineStrong, 0.42)

                Label {
                    anchors.centerIn: parent
                    text: "L"
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: 9
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: canToggleLocked
                    onClicked: root.toggleLockedTrackState()
                }
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

    Menu {
        id: trackMenu

        MenuItem {
            text: trackData.locked ? "Unlock Track" : "Lock Track"
            enabled: canToggleLocked
            onTriggered: root.toggleLockedTrackState()
        }

        MenuItem {
            text: trackData.targeted ? "Untarget Track" : "Target Track"
            enabled: canToggleTargeted
            onTriggered: root.toggleTargetedTrackState()
        }

        MenuItem {
            visible: trackData.trackKind === "video"
            text: trackData.visible ? "Hide Video Track" : "Show Video Track"
            enabled: canToggleVisibility
            onTriggered: root.togglePrimaryTrackState()
        }

        MenuItem {
            visible: trackData.trackKind === "audio"
            text: trackData.muted ? "Unmute Audio Track" : "Mute Audio Track"
            enabled: canToggleMuted
            onTriggered: root.togglePrimaryTrackState()
        }

        MenuItem {
            visible: trackData.trackKind === "audio"
            text: trackData.solo ? "Clear Solo" : "Solo Track"
            enabled: canToggleSolo
            onTriggered: root.toggleSoloTrackState()
        }
    }
}
