import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: card

    required property Item workspace
    property int assetIndex: 0
    property bool selected: workspace.catalogAssets && workspace.catalogAssets.selectedIndex === assetIndex
    property string thumbnailSource: workspace.assetThumb(assetIndex)
    property int ratingValue: workspace.assetRating(assetIndex)
    property bool pickedValue: workspace.assetPicked(assetIndex)
    property bool rejectedValue: workspace.assetRejected(assetIndex)
    property string badge: "RAW"
    property bool filmstrip: false
    property real fallbackAspectRatio: 1.5
    property bool filmstripAspectBadgeVisible: false
    property int filmstripImageMargin: 0
    property bool selectionBadgeVisible: true
    readonly property var ratioImage: filmstrip ? filmstripImage : thumbnailImage
    readonly property real sourceAspectRatio: ratioImage.status === Image.Ready &&
                                               ratioImage.sourceSize.width > 0 &&
                                               ratioImage.sourceSize.height > 0
                                               ? ratioImage.sourceSize.width / ratioImage.sourceSize.height
                                               : fallbackAspectRatio
    readonly property int assetRating: ratingValue
    readonly property bool assetPicked: pickedValue
    readonly property bool assetRejected: rejectedValue
    signal activated()
    signal doubleActivated()

    Theme { id: theme }

    radius: 0
    color: filmstrip ? (selected ? theme.alpha(workspace.mint, 0.08) : "transparent") : "transparent"
    border.width: filmstrip && selected ? 1 : 0
    border.color: filmstrip && selected ? workspace.mint : "transparent"
    clip: true

    Image {
        id: thumbnailImage
        anchors.fill: parent
        source: card.thumbnailSource
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: card.thumbnailSource.length > 0
        visible: !card.filmstrip && source.toString().length > 0
    }

    Rectangle {
        anchors.fill: parent
        color: theme.ink850
        visible: !card.filmstrip && card.thumbnailSource.length === 0
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0
        visible: card.filmstrip

        Rectangle {
            id: filmstripImageFrame
            objectName: "filmstripImageFrame"
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 2
            color: card.thumbnailSource.length > 0 ? "transparent" : theme.ink800
            border.width: 0
            border.color: "transparent"
            clip: true

            Image {
                id: filmstripImage
                objectName: "filmstripImage"
                anchors.fill: parent
                anchors.margins: card.filmstripImageMargin + (card.filmstrip ? 2 : 0)
                source: card.thumbnailSource
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: card.thumbnailSource.length > 0
                visible: source.toString().length > 0
            }

            Rectangle {
                objectName: "filmstripPlaceholder"
                anchors.fill: parent
                anchors.margins: 0
                color: theme.ink800
                visible: filmstripImage.source.toString().length === 0
            }

            Rectangle {
                objectName: "filmstripSelectionIndicator"
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                width: 14
                height: 14
                radius: 7
                visible: card.selected
                color: workspace.mint
                border.width: 0
                border.color: "transparent"

                AppIcon {
                    anchors.centerIn: parent
                    width: 9
                    height: 9
                    name: "check"
                    color: theme.ink950
                    visible: card.selected
                }
            }

            Label {
                objectName: "filmstripAspectBadge"
                visible: card.filmstripAspectBadgeVisible
                         && filmstripImage.status === Image.Ready
                         && workspace.aspectRatioText(filmstripImage.sourceSize.width,
                                                      filmstripImage.sourceSize.height).length > 0
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 5
                text: workspace.aspectRatioText(filmstripImage.sourceSize.width,
                                                filmstripImage.sourceSize.height)
                color: "#C8D0D6"
                font.family: theme.fontMono
                font.pixelSize: 8
                padding: 3
                background: Rectangle {
                    color: theme.alpha(theme.ink950, 0.74)
                    border.width: theme.chromeBorderWidth
                    border.color: workspace.hairlineStrong
                    radius: 2
                }
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        width: 18
        height: 18
        radius: 2
        visible: card.selectionBadgeVisible && !card.filmstrip &&
                 (workspace.selectionMode || card.selected)
        color: card.selected ? workspace.mint : theme.alpha(theme.ink950, 0.72)
        border.width: theme.chromeBorderWidth
        border.color: card.selected ? workspace.mintSoft : workspace.hairlineStrong

        AppIcon {
            anchors.centerIn: parent
            width: 11
            height: 11
            name: "check"
            color: theme.ink950
            visible: card.selected
        }
    }

    Rectangle {
        visible: !card.filmstrip && (card.assetPicked || card.assetRejected)
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        width: 18
        height: 18
        radius: 9
        color: card.assetRejected ? theme.alpha(theme.error, 0.88)
                                  : theme.alpha(workspace.mint, 0.88)
        border.width: theme.chromeBorderWidth
        border.color: card.assetRejected ? theme.error : workspace.mintSoft

        AppIcon {
            anchors.centerIn: parent
            width: 10
            height: 10
            name: card.assetRejected ? "close" : "check"
            color: theme.ink950
        }
    }

    Row {
        visible: !card.filmstrip && card.assetRating > 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 6
        spacing: 1

        Repeater {
            model: card.assetRating
            AppIcon {
                width: 10
                height: 10
                name: "star"
                color: workspace.mint
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: card.activated()
        onDoubleClicked: card.doubleActivated()
    }
}
