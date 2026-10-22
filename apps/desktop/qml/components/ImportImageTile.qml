import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property string filePath
    required property string fileName
    required property string fileUrl
    property bool selected: false
    readonly property string importImageTileContract: "import-image-tile:v1|preview:true|selection:true|readable-text:true|keyboard:true|hover:true|tooltip-full:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|file-name-fit:true|tooltip-full:true|accessibility-full:true"

    signal activated(string filePath)

    Theme { id: theme }

    width: 204
    height: 206
    radius: theme.radius12
    color: tileHover.hovered || activeFocus
           ? theme.alpha(theme.ink800, 0.98)
           : theme.alpha(theme.ink900, 0.94)
    border.width: selected || activeFocus ? 1 : 0
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.9)
                  : selected
                    ? theme.accentWarm400
                    : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: fileName
    Accessible.description: selected ? "Selected import image" : "Import image"
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: root.activated(root.filePath)
    Keys.onEnterPressed: root.activated(root.filePath)
    Keys.onSpacePressed: root.activated(root.filePath)

    HoverHandler {
        id: tileHover
        cursorShape: Qt.PointingHandCursor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space6

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 156
            radius: theme.radius10
            color: theme.ink950
            clip: true

            Image {
                anchors.fill: parent
                source: root.fileUrl
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
            }
        }

        Label {
            id: fileLabel
            Layout.fillWidth: true
            text: root.fileName
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: 11
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
            AppToolTip {
                id: sharedTooltip1
                targetItem: fileLabel
                visible: fileLabel.visible && (tileHover.hovered && fileLabel.contentWidth > fileLabel.width + 1)
                delay: 350
                timeout: 3600
                text: root.fileName
            }
        }
    }

    TapHandler {
        onTapped: root.activated(root.filePath)
    }
}
