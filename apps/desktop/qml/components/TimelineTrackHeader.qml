import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property var modelData
    required property var timelineDocument

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
                text: modelData.displayLabel
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            Label {
                text: modelData.trackKind === "audio" ? "A" : "V"
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
                color: modelData.trackKind === "video"
                       ? (modelData.visible ? theme.alpha(theme.accentCool500, 0.24) : theme.alpha(theme.ink700, 0.84))
                       : (modelData.muted ? theme.alpha(theme.error, 0.24) : theme.alpha(theme.ink700, 0.84))
                border.width: 1
                border.color: theme.alpha(theme.lineStrong, 0.42)

                Label {
                    anchors.centerIn: parent
                    text: modelData.trackKind === "video" ? "V" : "M"
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: 9
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (modelData.trackKind === "video")
                            timelineDocument.toggleTrackVisibility(modelData.trackIndex)
                        else
                            timelineDocument.toggleTrackMuted(modelData.trackIndex)
                    }
                }
            }

            Rectangle {
                implicitWidth: 20
                implicitHeight: 18
                radius: 5
                color: modelData.solo ? theme.alpha(theme.accentWarm500, 0.24) : theme.alpha(theme.ink700, 0.84)
                border.width: 1
                border.color: theme.alpha(theme.lineStrong, 0.42)
                visible: modelData.trackKind === "audio"

                Label {
                    anchors.centerIn: parent
                    text: "S"
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: 9
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: timelineDocument.toggleTrackSolo(modelData.trackIndex)
                }
            }

            Rectangle {
                implicitWidth: 20
                implicitHeight: 18
                radius: 5
                color: modelData.targeted ? theme.alpha(theme.accentWarm500, 0.24) : theme.alpha(theme.ink700, 0.84)
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
                    onClicked: timelineDocument.toggleTrackTargeted(modelData.trackIndex)
                }
            }

            Rectangle {
                implicitWidth: 20
                implicitHeight: 18
                radius: 5
                color: modelData.locked ? theme.alpha(theme.warning, 0.24) : theme.alpha(theme.ink700, 0.84)
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
                    onClicked: timelineDocument.toggleTrackLocked(modelData.trackIndex)
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

    Menu {
        id: trackMenu

        MenuItem {
            text: modelData.locked ? "Unlock Track" : "Lock Track"
            onTriggered: timelineDocument.toggleTrackLocked(modelData.trackIndex)
        }

        MenuItem {
            text: modelData.targeted ? "Untarget Track" : "Target Track"
            onTriggered: timelineDocument.toggleTrackTargeted(modelData.trackIndex)
        }

        MenuItem {
            visible: modelData.trackKind === "video"
            text: modelData.visible ? "Hide Video Track" : "Show Video Track"
            onTriggered: timelineDocument.toggleTrackVisibility(modelData.trackIndex)
        }

        MenuItem {
            visible: modelData.trackKind === "audio"
            text: modelData.muted ? "Unmute Audio Track" : "Mute Audio Track"
            onTriggered: timelineDocument.toggleTrackMuted(modelData.trackIndex)
        }

        MenuItem {
            visible: modelData.trackKind === "audio"
            text: modelData.solo ? "Clear Solo" : "Solo Track"
            onTriggered: timelineDocument.toggleTrackSolo(modelData.trackIndex)
        }
    }
}
