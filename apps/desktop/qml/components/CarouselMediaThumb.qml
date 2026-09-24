import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root

    property var asset: ({})
    property string thumbnailSource: String(asset.thumbnailSource || "")
    property string mediaType: String(asset.mediaType || "")
    property string tooltipText: String(asset.cameraLabel || asset.assetId || "Library asset")
    property bool online: Boolean(asset.online)
    readonly property string mediaLabel: mediaType === "video" ? "Video" : "Photo"
    readonly property string carouselMediaThumbContract: "carousel-media-thumb:v2|preview:true|online-state:true|click-double-click:true|drag-payload:true|copy-action:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|media-label-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string mediaSurfaceContract: "media-surface:v1|quick-library-thumbnail:stable|preview-dominant:true|label-zone:bottom|badge-corner:reserved|chrome:quiet"

    signal activated(var asset, var modifiers)
    signal doubleActivated(var asset)

    Theme { id: theme }

    width: 60
    height: 76
    radius: theme.radius6
    color: thumbHover.containsMouse
           ? theme.alpha(theme.ink850, 0.95)
           : theme.alpha(theme.ink900, 0.78)
    border.width: theme.chromeBorderWidth
    border.color: thumbHover.containsMouse
                  ? theme.alpha(theme.accentCool400, 0.46)
                  : theme.alpha(theme.lineSoft, 0.36)
    opacity: online ? 1 : 0.48
    clip: true
    Accessible.name: tooltipText
    Accessible.description: mediaLabel + (online ? "" : " unavailable")
    Accessible.role: Accessible.Button

    Image {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 54
        source: root.thumbnailSource
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        smooth: true
        visible: source.length > 0
    }

    AppIcon {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -8
        visible: root.thumbnailSource.length === 0
        name: root.mediaType === "video" ? "play" : "media"
        width: 16
        height: 16
        color: theme.textMuted
    }

    Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: theme.space4
        text: root.mediaLabel
        color: theme.textMuted
        font.family: theme.fontMono
        font.pixelSize: 8
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        maximumLineCount: 2
        elide: Text.ElideNone
    }

    MouseArea {
        id: thumbHover
        anchors.fill: parent
        hoverEnabled: true
        drag.target: dragProxy
        drag.axis: Drag.XAndYAxis
        drag.threshold: 6
        cursorShape: root.online ? Qt.PointingHandCursor : Qt.ForbiddenCursor
        onClicked: function(mouse) {
            root.activated(root.asset, mouse.modifiers)
        }
        onDoubleClicked: root.doubleActivated(root.asset)
    }

    Item {
        id: dragProxy
        width: 1
        height: 1
        Drag.active: thumbHover.drag.active && root.online
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction
        Drag.proposedAction: Qt.CopyAction
        Drag.mimeData: ({
            "application/x-carousel-media": JSON.stringify(root.asset)
        })
        Drag.hotSpot.x: 0
        Drag.hotSpot.y: 0
    }

    ToolTip.visible: thumbHover.containsMouse
    ToolTip.text: root.tooltipText
    ToolTip.delay: 500
    ToolTip.timeout: 3600
}
