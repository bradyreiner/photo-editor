import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property string filePath
    required property string fileName
    required property string fileUrl
    property bool selected: false
    property bool partiallySelected: false
    property bool contextualSelection: false
    property bool selectionFocus: false
    property bool isVideo: false
    property string extension: ""
    property string detailText: ""
    property string previewSource: ""
    property bool showDetails: false
    readonly property bool previewReady: previewImage.status === Image.Ready
    readonly property string normalizedExtension: extension.trim().toLowerCase().charAt(0) === "."
                                                   ? extension.trim().toLowerCase().slice(1)
                                                   : extension.trim().toLowerCase()
    readonly property bool showsVideoIndicator: isVideo
                                                 || /^(mov|mp4|m4v|avi|mkv|webm|mpeg|mpg)$/.test(normalizedExtension)
    readonly property string mediaDetailText: normalizedExtension.length > 0 && detailText.trim().length > 0
                                              ? normalizedExtension.toUpperCase() + " · " + detailText.trim()
                                              : detailText.trim().length > 0
                                                ? detailText.trim()
                                                : normalizedExtension.toUpperCase()
    readonly property string importImageTileContract: "import-image-tile:v1|preview:true|selection:true|readable-text:true|keyboard:true|hover:true|tooltip-full:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|file-name-fit:true|tooltip-full:true|accessibility-full:true"

    signal activated(string filePath)
    signal selectionRequested(string filePath, int modifiers)

    Theme { id: theme }

    width: 204
    height: 206
    radius: theme.radius4
    color: !enabled
           ? theme.alpha(theme.ink850, 0.46)
           : selected
             ? theme.alpha(theme.editorSelectionFill, 0.6)
             : (tileHover.hovered ? theme.editorControlHover : "transparent")
    border.width: activeFocus || selectionFocus ? theme.focusBorderWidth : theme.hairlineWidth
    border.color: activeFocus || selectionFocus
                  ? theme.focusRing
                  : selected
                    ? theme.alpha(theme.editorSelectionEdge, 0.55)
                    : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: fileName
    Accessible.description: (selected ? "Selected " : "")
                            + (showsVideoIndicator ? "video" : "photo")
                            + " at " + filePath
                            + (mediaDetailText.length > 0 ? ". " + mediaDetailText : "")
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: if (root.enabled) root.activated(root.filePath)
    Keys.onEnterPressed: if (root.enabled) root.activated(root.filePath)
    Keys.onSpacePressed: if (root.enabled) root.activated(root.filePath)
    opacity: enabled ? 1.0 : 0.62

    Behavior on color {
        ColorAnimation { duration: theme.durationFast }
    }

    HoverHandler {
        id: tileHover
        enabled: root.enabled
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.space6
        spacing: theme.space4

        Rectangle {
            id: previewWell
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: theme.scaled(84)
            Layout.preferredHeight: theme.scaled(132)
            radius: theme.radius4
            color: theme.mediaCanvas
            border.width: 0
            clip: true

            Image {
                id: previewImage
                objectName: "importSourceThumbnail"
                anchors.fill: parent
                source: root.previewSource.length > 0
                        ? root.previewSource
                        : (root.showsVideoIndicator ? "" : "image://import-preview/" + encodeURIComponent(root.filePath))
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                visible: status === Image.Ready
            }

            Rectangle {
                anchors.fill: parent
                visible: previewImage.status !== Image.Ready
                color: theme.mediaCanvas

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: theme.space4

                    AppIcon {
                        visible: previewImage.status !== Image.Loading
                        Layout.alignment: Qt.AlignHCenter
                        name: root.showsVideoIndicator ? "video" : "media"
                        implicitWidth: theme.scaled(22)
                        implicitHeight: theme.scaled(22)
                        color: theme.textMuted
                    }

                    Label {
                        visible: false
                        Layout.alignment: Qt.AlignHCenter
                        text: root.showsVideoIndicator
                              ? "VIDEO"
                              : (root.normalizedExtension.length > 0 ? root.normalizedExtension.toUpperCase() : "PREVIEW")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                        font.weight: theme.weightControl
                    }
                }
            }

            OpticalActivityIndicator {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: theme.space8
                width: theme.scaled(16)
                height: width
                visible: previewImage.status === Image.Loading
                active: visible
            }

            Rectangle {
                visible: root.selected || root.partiallySelected || tileHover.hovered || root.activeFocus || root.selectionFocus
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: theme.space6
                width: theme.scaled(20)
                height: width
                radius: theme.radius4
                color: root.selected || root.partiallySelected ? theme.textStrong : theme.alpha(theme.ink950, 0.84)
                border.width: theme.hairlineWidth
                border.color: root.selected ? theme.textStrong : theme.textMuted

                AppIcon {
                    anchors.centerIn: parent
                    visible: root.selected && !root.partiallySelected
                    name: "check"
                    implicitWidth: theme.scaled(12)
                    implicitHeight: theme.scaled(12)
                    color: theme.ink950
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: theme.scaled(9)
                    height: theme.scaled(2)
                    visible: root.partiallySelected
                    color: theme.ink950
                }
            }

            Rectangle {
                visible: root.showsVideoIndicator && previewImage.status === Image.Ready
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: theme.space6
                width: theme.scaled(25)
                height: width
                radius: theme.radius4
                color: theme.alpha(theme.ink950, 0.86)
                border.width: theme.hairlineWidth
                border.color: theme.alpha(theme.lineSoft, 0.58)

                AppIcon {
                    anchors.centerIn: parent
                    name: "play"
                    implicitWidth: theme.scaled(12)
                    implicitHeight: theme.scaled(12)
                    color: theme.textStrong
                }
            }
        }

        Label {
            id: fileLabel
            Layout.fillWidth: true
            text: root.fileName
            color: root.enabled ? theme.textStrong : theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: theme.weightControl
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
            AppToolTip {
                id: sharedTooltip1
                targetItem: fileLabel
                visible: fileLabel.visible && (tileHover.hovered && (fileLabel.truncated || fileLabel.contentHeight > fileLabel.height + 1))
                delay: 350
                timeout: 3600
                text: root.fileName
            }
        }

        Label {
            id: detailLabel
            visible: root.showDetails && root.mediaDetailText.length > 0
            Layout.fillWidth: true
            text: root.mediaDetailText
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
            elide: Text.ElideNone
            AppToolTip {
                id: sharedTooltip2
                targetItem: detailLabel
                visible: detailLabel.visible && tileHover.hovered && detailLabel.contentWidth > detailLabel.width + 1
                delay: 350
                timeout: 3600
                text: root.mediaDetailText
            }
        }
    }

    TapHandler {
        id: tileTap
        enabled: root.enabled
        onTapped: {
            root.forceActiveFocus()
            if (root.contextualSelection)
                root.selectionRequested(root.filePath, tileTap.point.modifiers)
            else
                root.activated(root.filePath)
        }
    }
}
