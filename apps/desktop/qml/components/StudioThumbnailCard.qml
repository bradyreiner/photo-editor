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
    border.width: selected ? theme.scaled(2) : 0
    border.color: selected ? "#E3BD6B" : "transparent"
    clip: true

    ColorManagedImage {
        id: thumbnailImage
        anchors.fill: parent
        imageSource: card.thumbnailSource
        displayController: workspace.editorPreviewController || null
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

            ColorManagedImage {
                id: filmstripImage
                objectName: "filmstripImage"
                anchors.fill: parent
                anchors.margins: card.filmstripImageMargin + (card.filmstrip ? 2 : 0)
                imageSource: card.thumbnailSource
                displayController: workspace.editorPreviewController || null
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
                font.family: theme.fontUi
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
        objectName: "thumbnailSelectionEdge"
        anchors.fill: parent
        color: "transparent"
        visible: card.selected && (card.filmstrip || card.selectionBadgeVisible)
        border.width: theme.scaled(2)
        border.color: "#E3BD6B"
    }

    Rectangle {
        visible: !card.filmstrip && (card.assetPicked || card.assetRejected)
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        width: 18
        height: 18
        radius: 3
        color: card.assetRejected ? theme.alpha(theme.error, 0.88)
                                  : theme.alpha(workspace.mint, 0.88)
        border.width: theme.chromeBorderWidth
        border.color: card.assetRejected ? theme.error : workspace.mintSoft

        AppIcon {
            anchors.centerIn: parent
            width: 10
            height: 10
            name: card.assetRejected ? "close" : "flag"
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
                width: theme.scaled(16)
                height: theme.scaled(16)
                name: "star-gold"
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
