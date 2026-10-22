import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property int assetIndex: -1
    property var asset: ({})
    property bool selected: false
    readonly property string assetId: String(asset.assetId || "")
    readonly property string cameraLabel: String(asset.cameraLabel || "")
    readonly property string thumbnailSource: String(asset.thumbnailSource || "")
    readonly property string mediaType: String(asset.mediaType || "")
    readonly property string durationText: String(asset.durationText || "")
    readonly property bool online: Boolean(asset.online)
    readonly property string displayLabel: cameraLabel.length > 0 ? cameraLabel : assetId
    readonly property string mediaLabel: mediaType === "photo"
                                         ? "Photo"
                                         : mediaType === "audio"
                                           ? "Audio"
                                           : mediaType === "timeline"
                                             ? "Timeline"
                                             : "Video"
    readonly property string galleryAssetCardContract: "gallery-asset-card:v1|preview:true|selection:true|online-state:true|keyboard:true|double-click:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|metadata-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string cardAnatomyContract: "card-anatomy:v1|preview-dominant:true|content:title-wrap|metadata:bottom-wrap|selection:quiet-border|keyboard:focus-ring|online-state:opacity"

    signal selectedRequested(int assetIndex)
    signal activated(int assetIndex)

    Theme { id: theme }

    function gcd(a, b) {
        let left = Math.abs(Math.round(a))
        let right = Math.abs(Math.round(b))
        while (right > 0) {
            const next = left % right
            left = right
            right = next
        }
        return Math.max(1, left)
    }

    function aspectRatioText(width, height) {
        if (width <= 0 || height <= 0)
            return ""
        const divisor = gcd(width, height)
        return Math.round(width / divisor) + ":" + Math.round(height / divisor)
    }

    width: GridView.view ? GridView.view.cellWidth : 176
    height: GridView.view ? GridView.view.cellHeight : 212
    radius: theme.radius12
    color: selected ? theme.alpha(theme.accentWarm500, 0.16) : theme.alpha(theme.ink900, 0.88)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.88)
                  : selected
                    ? theme.accentWarm300
                    : theme.alpha(theme.lineSoft, 0.34)
    opacity: online ? 1 : 0.56
    clip: true
    activeFocusOnTab: true
    Accessible.name: displayLabel
    Accessible.description: mediaLabel + (durationText.length > 0 ? ", " + durationText : "")
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: root.activated(root.assetIndex)
    Keys.onEnterPressed: root.activated(root.assetIndex)
    Keys.onSpacePressed: root.selectedRequested(root.assetIndex)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space8

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 132
            radius: theme.radius10
            color: "#0b1014"
            clip: true

            Image {
                id: cardPreview
                anchors.fill: parent
                source: root.thumbnailSource
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                visible: root.thumbnailSource.length > 0
            }

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: theme.space8
                visible: cardPreview.status === Image.Ready &&
                         root.aspectRatioText(cardPreview.sourceSize.width, cardPreview.sourceSize.height).length > 0
                radius: theme.radius8
                color: theme.alpha(theme.ink950, 0.88)
                border.width: theme.chromeBorderWidth
                border.color: theme.alpha(theme.lineSoft, 0.52)
                implicitWidth: ratioLabel.implicitWidth + theme.space12
                implicitHeight: ratioLabel.implicitHeight + theme.space6

                Label {
                    id: ratioLabel
                    anchors.centerIn: parent
                    text: root.aspectRatioText(cardPreview.sourceSize.width, cardPreview.sourceSize.height)
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: 10
                }
            }

            Label {
                anchors.centerIn: parent
                visible: root.thumbnailSource.length === 0
                text: root.mediaType === "timeline" ? "Timeline" : "Preview"
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 11
            }
        }

        Label {
            id: titleLabel
            Layout.fillWidth: true
            text: root.displayLabel
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: 12
            font.weight: Font.Medium
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
            AppToolTip {
                id: sharedTooltip1
                targetItem: titleLabel
                visible: titleLabel.visible && (cardHover.hovered && titleLabel.contentHeight > titleLabel.height + 1)
                delay: 350
                timeout: 3600
                text: root.displayLabel
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space6

            Label {
                text: root.mediaLabel
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
            }

            Item { Layout.fillWidth: true }

            Label {
                Layout.maximumWidth: parent.width * 0.58
                text: root.durationText
                visible: root.durationText.length > 0
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }
    }

    HoverHandler {
        id: cardHover
        cursorShape: root.online ? Qt.PointingHandCursor : Qt.ForbiddenCursor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: root.selectedRequested(root.assetIndex)
        onDoubleTapped: root.activated(root.assetIndex)
    }
}
