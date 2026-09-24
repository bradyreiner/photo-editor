pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia

Rectangle {
    id: root

    required property var documentController
    property string groupId: ""
    property int angleIndex: 0
    property string angleLabel: "Angle"
    property real sequenceSeconds: 0
    property bool sequencePlaying: false
    readonly property bool activeAngle: previewState.active === true
    readonly property bool audioActive: previewState.audioActive === true
    property bool monitorEnabled: true
    property bool liveDecodeEnabled: true
    readonly property bool programRole: activeAngle
    property var previewState: ({})

    signal angleActivated(int angleIndex)

    readonly property string monitorContract: "timeline-multicam-angle-monitor:v1|source:controller-exact-trim-retime-map|sync:program-sequence-clock|decode:muted-preview-only|state:active-audio-unavailable|interaction:cut-at-playhead|release:source-cleared-when-closed"
    readonly property bool decodedPreviewReady: playerLoader.item
                                                && (playerLoader.item.mediaStatus === MediaPlayer.LoadedMedia
                                                    || playerLoader.item.mediaStatus === MediaPlayer.BufferingMedia
                                                    || playerLoader.item.mediaStatus === MediaPlayer.BufferedMedia)

    Theme { id: theme }

    radius: theme.radiusSurface
    color: theme.mediaCanvas
    border.width: root.activeAngle ? theme.scaled(2) : theme.hairlineWidth
    border.color: root.activeAngle
                  ? theme.focusRing
                  : theme.alpha(theme.lineSoft, 0.28)
    clip: true
    Accessible.name: (root.programRole ? "Program, " : "")
                     + "Camera " + String(root.angleIndex + 1)
                     + ", " + root.angleLabel
    Accessible.description: root.previewState.available === false
                            ? "Unavailable at the current playhead"
                            : root.activeAngle ? "Active Program angle" : "Available camera angle"

    function refreshPreview() {
        const player = playerLoader.item
        if (!root.monitorEnabled || !root.documentController || root.groupId.length === 0) {
            root.previewState = ({})
            if (player) {
                player.pause()
                player.source = ""
            }
            return
        }
        const state = root.documentController.multicamAnglePreviewState(
            root.groupId, root.angleIndex, root.sequenceSeconds)
        root.previewState = state || ({})
        const nextSource = root.liveDecodeEnabled
                           ? String(root.previewState.sourceUrl || "") : ""
        if (player && String(player.source || "") !== nextSource)
            player.source = nextSource
        syncPlayer()
    }

    function syncPlayer() {
        const player = playerLoader.item
        if (!player)
            return
        if (!root.monitorEnabled || !root.liveDecodeEnabled
                || root.previewState.available !== true
                || String(root.previewState.sourceUrl || "").length === 0) {
            player.pause()
            return
        }
        const target = Math.max(0, Number(root.previewState.sourceSeconds || 0) * 1000)
        const rate = Number(root.previewState.effectivePlaybackRate || 0)
        player.playbackRate = Math.max(0.01, Math.abs(rate))
        if (Math.abs(player.position - target) > 45)
            player.position = target
        if (root.sequencePlaying && rate > 0.0001)
            player.play()
        else
            player.pause()
    }

    function sourceTimeText(seconds) {
        const safe = Math.max(0, Number(seconds || 0))
        const totalMilliseconds = Math.floor(safe * 1000)
        const totalSeconds = Math.floor(totalMilliseconds / 1000)
        const hours = Math.floor(totalSeconds / 3600)
        const minutes = Math.floor((totalSeconds % 3600) / 60)
        const remainingSeconds = totalSeconds % 60
        const milliseconds = totalMilliseconds % 1000
        return String(hours).padStart(2, "0")
                + ":" + String(minutes).padStart(2, "0")
                + ":" + String(remainingSeconds).padStart(2, "0")
                + "." + String(milliseconds).padStart(3, "0")
    }

    Loader {
        id: playerLoader
        active: root.monitorEnabled && root.liveDecodeEnabled
        sourceComponent: Component {
            MediaPlayer {
                videoOutput: angleVideo
                audioOutput: AudioOutput { muted: true }
                onMediaStatusChanged: root.syncPlayer()
            }
        }
        onLoaded: root.refreshPreview()
    }

    VideoOutput {
        id: angleVideo
        objectName: "timelineMulticamAngleVideo_" + String(root.angleIndex + 1)
        anchors.fill: parent
        anchors.margins: root.activeAngle ? theme.scaled(2) : theme.hairlineWidth
        fillMode: VideoOutput.PreserveAspectFit
        visible: root.previewState.available === true && root.liveDecodeEnabled
    }

    Image {
        anchors.fill: angleVideo
        source: String(root.previewState.posterSource || "")
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: true
        visible: root.previewState.available === true
                 && (!root.liveDecodeEnabled
                     || !playerLoader.item
                     || !root.decodedPreviewReady)
                 && source.toString().length > 0
    }

    Rectangle {
        anchors.fill: parent
        visible: root.previewState.available === false
        color: theme.alpha(theme.ink950, 0.74)

        Label {
            anchors.centerIn: parent
            text: "NO MEDIA"
            color: theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: theme.typeCaption
            font.weight: Font.DemiBold
        }
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: theme.space6
        spacing: theme.space4

        Rectangle {
            implicitWidth: angleNumberLabel.implicitWidth + theme.space10
            implicitHeight: angleNumberLabel.implicitHeight + theme.space4
            radius: theme.radius4
            color: root.activeAngle
                   ? theme.alpha(theme.focusRing, 0.92)
                   : theme.alpha(theme.ink950, 0.76)

            Label {
                id: angleNumberLabel
                anchors.centerIn: parent
                text: root.programRole || root.activeAngle
                      ? "PROGRAM · " + String(root.angleIndex + 1)
                      : String(root.angleIndex + 1)
                color: root.activeAngle ? theme.ink950 : theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: theme.typeCaption
                font.weight: Font.DemiBold
            }
        }

        Item { Layout.fillWidth: true }

        Rectangle {
            visible: root.audioActive
            implicitWidth: audioLabel.implicitWidth + theme.space8
            implicitHeight: audioLabel.implicitHeight + theme.space4
            radius: theme.radius4
            color: theme.alpha(theme.ink950, 0.76)

            Label {
                id: audioLabel
                anchors.centerIn: parent
                text: "AUDIO"
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                font.weight: Font.DemiBold
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: angleFooter.implicitHeight + theme.space8
        color: theme.alpha(theme.ink950, 0.76)

        RowLayout {
            id: angleFooter
            anchors.fill: parent
            anchors.leftMargin: theme.space8
            anchors.rightMargin: theme.space8
            spacing: theme.space6

            Label {
                Layout.fillWidth: true
                text: root.angleLabel
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                elide: Text.ElideRight
            }

            Label {
                text: root.sourceTimeText(Number(root.previewState.sourceSeconds || 0))
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
            }
        }
    }

    TapHandler {
        enabled: root.previewState.available === true
        onTapped: root.angleActivated(root.angleIndex)
    }

    onGroupIdChanged: refreshPreview()
    onAngleIndexChanged: refreshPreview()
    onSequenceSecondsChanged: refreshPreview()
    onSequencePlayingChanged: syncPlayer()
    onMonitorEnabledChanged: refreshPreview()
    onLiveDecodeEnabledChanged: refreshPreview()
    Component.onCompleted: refreshPreview()
}
