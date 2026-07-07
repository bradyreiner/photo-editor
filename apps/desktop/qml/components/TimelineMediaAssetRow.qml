import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string assetId
    required property string cameraLabel
    required property string filePath
    required property string thumbnailSource
    required property string mediaType
    required property string durationText
    property string mediaSearchText: ""
    readonly property string titleText: cameraLabel.length > 0 ? cameraLabel : assetId
    readonly property bool matchesSearch: mediaSearchText.length === 0
                                          || titleText.toLowerCase().indexOf(mediaSearchText.toLowerCase()) >= 0
    readonly property int previewColumnWidth: 84
    readonly property int actionZoneWidth: 92
    readonly property string timelineMediaAssetRowContract: "timeline-media-asset-row:v1|preview:true|source-insert-overwrite:true|keyboard:true|hover:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|metadata-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string rowAnatomyContract: "row-anatomy:v1|preview-zone:fixed-84|content:title-metadata-wrap|action-zone:fixed-92|actions:source-insert-overwrite|hover:neutral|focus:ring"

    signal sourceRequested(string assetId, string filePath, string titleText, string thumbnailSource)
    signal insertRequested(string assetId, string filePath, string titleText, string thumbnailSource)
    signal overwriteRequested(string assetId, string filePath, string titleText, string thumbnailSource)

    Theme { id: theme }

    visible: mediaType === "video" && matchesSearch
    width: ListView.view ? ListView.view.width : 360
    implicitHeight: Math.max(112, mediaRow.implicitHeight + theme.space16)
    height: visible ? implicitHeight : 0
    radius: theme.radius8
    color: rowHover.hovered || activeFocus
           ? theme.alpha(theme.ink850, 0.96)
           : theme.alpha(theme.ink900, 0.9)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.9)
                  : rowHover.hovered
                    ? theme.alpha(theme.accentWarm400, 0.36)
                    : theme.alpha(theme.lineSoft, 0.34)
    clip: true
    activeFocusOnTab: true
    Accessible.name: titleText
    Accessible.description: "Video clip, " + durationText
    Accessible.role: Accessible.ListItem
    Keys.onReturnPressed: root.sourceRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
    Keys.onEnterPressed: root.sourceRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
    Keys.onSpacePressed: root.sourceRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)

    ToolTip.visible: rowHover.hovered && titleLabel.contentWidth > titleLabel.width + 1
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: titleText

    HoverHandler { id: rowHover }

    RowLayout {
        id: mediaRow
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space8

        Rectangle {
            Layout.preferredWidth: root.previewColumnWidth
            Layout.minimumWidth: root.previewColumnWidth
            Layout.preferredHeight: 64
            radius: theme.radius6
            color: "#0b1014"
            clip: true

            Image {
                anchors.fill: parent
                source: root.thumbnailSource
                fillMode: Image.PreserveAspectCrop
                cache: true
                asynchronous: true
                visible: source.toString().length > 0
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: root.titleText
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.Medium
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: "Video  •  " + root.durationText
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 9
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }

        ColumnLayout {
            Layout.minimumWidth: root.actionZoneWidth
            Layout.preferredWidth: root.actionZoneWidth
            Layout.alignment: Qt.AlignVCenter
            spacing: theme.space4

            ActionButton {
                text: "Source"
                compact: true
                minimumReadableWidth: root.actionZoneWidth
                tooltipText: "Load this clip into the source monitor."
                Layout.fillWidth: true
                onClicked: root.sourceRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
            }

            ActionButton {
                text: "Insert"
                compact: true
                minimumReadableWidth: root.actionZoneWidth
                tooltipText: "Insert at playhead."
                Layout.fillWidth: true
                onClicked: root.insertRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
            }

            ActionButton {
                text: "Overwrite"
                compact: true
                minimumReadableWidth: root.actionZoneWidth
                tooltipText: "Overwrite at playhead."
                Layout.fillWidth: true
                onClicked: root.overwriteRequested(root.assetId, root.filePath, root.titleText, root.thumbnailSource)
            }
        }
    }
}
