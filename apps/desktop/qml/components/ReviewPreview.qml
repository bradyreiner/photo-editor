import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property string title
    property string subtitle: ""
    property string imageSource: ""
    property bool selected: false
    property bool focusCheckEnabled: false
    property bool photoAsset: true
    property int rating: 0
    property bool picked: false
    property bool rejected: false
    property string burstText: ""
    property bool showDoubleClickHint: false

    signal activated()
    signal doubleActivated()

    Theme { id: theme }

    readonly property string reviewCardContract: "review-preview-card:v1|image-dominant:true|selection:" +
                                                 String(selected) +
                                                 "|rating:" +
                                                 String(rating > 0) +
                                                 "|pick:" +
                                                 String(picked) +
                                                 "|reject:" +
                                                 String(rejected) +
                                                 "|burst:" +
                                                 String(burstText.length > 0) +
                                                 "|focus-loupe:" +
                                                 String(focusCheckEnabled && photoAsset) +
                                                 "|badges:status-chip|no-elide:true|double-click:true"
    readonly property color reviewStateColor: rejected ? theme.error
                                           : picked ? theme.success
                                           : selected ? theme.accentWarm400
                                           : theme.textMuted

    radius: theme.radius12
    color: selected ? theme.alpha(theme.accentWarm400, 0.08)
                    : theme.alpha(theme.glassBase, 0.72)
    border.width: selected || picked || rejected ? 1 : 0
    border.color: theme.alpha(reviewStateColor, selected ? 0.62 : 0.46)
    clip: true

    property real normalizedLoupeX: 0.5
    property real normalizedLoupeY: 0.5
    property bool hoverLoupeVisible: false

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        visible: root.selected || root.picked || root.rejected
        color: root.reviewStateColor
        z: 3
    }

    Rectangle {
        id: imageFrame
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: infoColumn.top
        anchors.bottomMargin: 0
        color: "#0f1418"

        Image {
            id: previewImage
            anchors.fill: parent
            anchors.margins: 2
            source: root.imageSource
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectFit
        }

        Row {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 10
            spacing: 6

            StatusChip {
                visible: root.rating > 0
                width: implicitWidth
                height: implicitHeight
                text: "R" + root.rating
                tone: "neutral"
                compact: true
                dotVisible: false
                tooltipText: "Rating " + root.rating
            }

            StatusChip {
                visible: root.picked || root.rejected
                width: implicitWidth
                height: implicitHeight
                text: root.rejected ? "Reject" : "Pick"
                tone: root.rejected ? "error" : "success"
                compact: true
                dotVisible: false
            }

            StatusChip {
                visible: root.burstText.length > 0
                width: implicitWidth
                height: implicitHeight
                text: root.burstText
                tone: "info"
                compact: true
                dotVisible: false
            }
        }

        StatusChip {
            visible: root.focusCheckEnabled && root.photoAsset
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 10
            width: implicitWidth
            height: implicitHeight
            text: root.hoverLoupeVisible ? "Focus On" : "Focus"
            tone: root.hoverLoupeVisible ? "accent" : "neutral"
            compact: true
            dotVisible: false
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.activated()
            onDoubleClicked: root.doubleActivated()
            onExited: root.hoverLoupeVisible = false
            onPositionChanged: function(mouse) {
                if (!root.focusCheckEnabled || !root.photoAsset || previewImage.paintedWidth <= 0 || previewImage.paintedHeight <= 0) {
                    root.hoverLoupeVisible = false
                    return
                }

                const imageX = previewImage.x + (previewImage.width - previewImage.paintedWidth) / 2
                const imageY = previewImage.y + (previewImage.height - previewImage.paintedHeight) / 2
                if (mouse.x < imageX || mouse.x > imageX + previewImage.paintedWidth
                        || mouse.y < imageY || mouse.y > imageY + previewImage.paintedHeight) {
                    root.hoverLoupeVisible = false
                    return
                }

                root.normalizedLoupeX = (mouse.x - imageX) / Math.max(1, previewImage.paintedWidth)
                root.normalizedLoupeY = (mouse.y - imageY) / Math.max(1, previewImage.paintedHeight)
                root.hoverLoupeVisible = true
            }
        }

        Rectangle {
            width: 144
            height: 144
            radius: 72
            border.width: 1
            border.color: theme.alpha(theme.lineStrong, 0.94)
            color: theme.ink950
            clip: true
            visible: root.hoverLoupeVisible && root.focusCheckEnabled && root.photoAsset && root.imageSource.length > 0
            x: Math.max(10, Math.min(parent.width - width - 10, parent.width * 0.58))
            y: Math.max(10, Math.min(parent.height - height - 10, parent.height * 0.12))

            Image {
                anchors.centerIn: parent
                source: root.imageSource
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                width: Math.max(1, previewImage.paintedWidth * 2.4)
                height: Math.max(1, previewImage.paintedHeight * 2.4)
                x: parent.width / 2 - root.normalizedLoupeX * width
                y: parent.height / 2 - root.normalizedLoupeY * height
            }

            Rectangle {
                anchors.centerIn: parent
                width: 18
                height: 18
                radius: 9
                color: "transparent"
                border.width: 1
                border.color: theme.alpha(theme.textStrong, 0.85)
            }
        }
    }

    ColumnLayout {
        id: infoColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 2

        Label {
            Layout.fillWidth: true
            text: root.title
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: 12
            font.weight: Font.Medium
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
        }

        Label {
            Layout.fillWidth: true
            visible: root.subtitle.length > 0 || root.showDoubleClickHint
            text: root.showDoubleClickHint && root.subtitle.length === 0 ? "Double-click to open in Edit"
                                                                  : root.subtitle
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 10
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
        }
    }
}
