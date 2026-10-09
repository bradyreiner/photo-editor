pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import QtQml
import "../../components"

ColumnLayout {
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselVideoPreviewDialog
    property alias player: carouselVideoPreviewPlayer
    anchors.fill: parent
    anchors.margins: 16
    spacing: 10

    RowLayout {
        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: "Board video playback"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: "Previewing " + root.formatVideoTime(
                          carouselVideoPreviewDialog.trimOutSeconds -
                          carouselVideoPreviewDialog.trimInSeconds) +
                      " from the active board or video layer"
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }

        ActionButton {
            compact: true
            showText: false
            iconName: "close"
            tooltipText: "Close video preview"
            onClicked: carouselVideoPreviewDialog.close()
        }
    }

    Rectangle {
        id: carouselVideoBoardFrame
        objectName: "carouselVideoBoardPlaybackSurface"
        Layout.alignment: Qt.AlignHCenter
        Layout.fillHeight: true
        Layout.preferredWidth: Math.min(parent.width, height * carouselVideoPreviewDialog.boardAspect)
        Layout.minimumWidth: 220
        Layout.maximumWidth: parent.width
        radius: theme.radius8
        color: "#000000"
        clip: true
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.6)

        VideoOutput {
            id: carouselVideoPreviewOutput
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectCrop
        }

        Label {
            anchors.centerIn: parent
            visible: carouselVideoPreviewPlayer.mediaStatus === MediaPlayer.InvalidMedia ||
                     carouselVideoPreviewDialog.sourceUrl.toString().length === 0
            text: "Video preview unavailable"
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 11
        }
    }

    MediaPlayer {
        id: carouselVideoPreviewPlayer
        source: carouselVideoPreviewDialog.sourceUrl
        videoOutput: carouselVideoPreviewOutput
        audioOutput: carouselVideoPreviewAudio

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia ||
                    mediaStatus === MediaPlayer.BufferedMedia)
                Qt.callLater(carouselVideoPreviewDialog.seekPoster)
        }
        onPositionChanged: {
            const trimInMs = Math.round(carouselVideoPreviewDialog.trimInSeconds * 1000)
            const trimOutMs = Math.round(carouselVideoPreviewDialog.trimOutSeconds * 1000)
            if (position < trimInMs - 2) {
                position = trimInMs
            } else if (position >= trimOutMs) {
                if (carouselVideoPreviewDialog.loopIntent) {
                    position = trimInMs
                    play()
                } else {
                    pause()
                    if (Math.abs(position - trimOutMs) > 2)
                        position = trimOutMs
                }
            }
        }
    }

    AudioOutput {
        id: carouselVideoPreviewAudio
        muted: carouselVideoPreviewDialog.audioPolicy === "mute"
        volume: 1.0
    }

    ToolValueSlider {
        id: carouselVideoPreviewScrubber
        objectName: "carouselVideoPreviewScrubber"
        Layout.fillWidth: true
        label: "Video preview position"
        valueText: root.formatVideoTime(value / 1000)
        tooltipText: "Scrub the trimmed video preview"
        from: carouselVideoPreviewDialog.trimInSeconds * 1000
        to: carouselVideoPreviewDialog.trimOutSeconds * 1000
        value: carouselVideoPreviewPlayer.position
        onMoved: carouselVideoPreviewPlayer.position = Math.round(value)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: root.formatVideoTime(carouselVideoPreviewPlayer.position / 1000) +
                  "  ·  " +
                  (carouselVideoPreviewDialog.audioPolicy === "preserve" ? "Audio on" : "Muted") +
                  "  ·  " +
                  (carouselVideoPreviewDialog.loopIntent ? "Loops" : "Plays once")
            color: theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: 10
            elide: Text.ElideNone
        }

        ActionButton {
            compact: true
            text: "Poster frame"
            onClicked: carouselVideoPreviewDialog.seekPoster()
        }

        ActionButton {
            compact: true
            tone: "primary"
            iconName: carouselVideoPreviewPlayer.playbackState === MediaPlayer.PlayingState
                      ? "pause" : "play"
            text: carouselVideoPreviewPlayer.playbackState === MediaPlayer.PlayingState
                  ? "Pause" : "Play trim"
            onClicked: {
                if (carouselVideoPreviewPlayer.playbackState === MediaPlayer.PlayingState)
                    carouselVideoPreviewPlayer.pause()
                else
                    carouselVideoPreviewDialog.playTrimmed()
            }
        }
    }
}
