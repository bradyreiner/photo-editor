pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property string title: "Clip"
    property string metadata: ""
    property string mediaType: "video"
    property var statusBadges: []
    property real horizontalInset: 0
    property real rightInset: horizontalInset
    property bool selected: false
    readonly property bool pointerInside: clipContentHover.hovered
    readonly property string timelineClipContentContract: "timeline-clip-content:v2|title-fit:true|metadata-fit:true|status-strip:compact-consistent|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|title-fit:true|metadata-fit:true|status-fit:true|tooltip-full:true|accessibility-full:true"

    Theme { id: theme }

    clip: true
    Accessible.name: title
    Accessible.description: metadata.length > 0
                            ? metadata + (selected ? ". Selected clip." : "")
                            : selected ? "Selected clip" : "Clip"
    Accessible.role: Accessible.StaticText

    HoverHandler {
        id: clipContentHover
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space8 + root.horizontalInset
        anchors.rightMargin: theme.space8 + root.rightInset
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6
        spacing: theme.space2

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space4

            MediaTypeIcon {
                objectName: "timelineClipMediaIcon"
                mediaType: root.mediaType
                // Keep the title usable when a clip is zoomed down to a sliver.
                visible: root.width - root.horizontalInset - root.rightInset >= theme.scaled(80)
                color: root.selected ? theme.textStrong : theme.textMain
                Layout.preferredWidth: theme.scaled(13)
                Layout.preferredHeight: theme.scaled(13)
            }

            Label {
                id: titleLabel
                objectName: "timelineClipTitleLabel"
                Layout.fillWidth: true
                text: root.title
                color: root.selected ? theme.textStrong : theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeLabel
                font.weight: root.selected ? Font.DemiBold : Font.Medium
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 7
                elide: Text.ElideNone
                ToolTip.visible: titleHover.hovered && contentWidth > width + 1
                ToolTip.delay: 350
                ToolTip.timeout: 3600
                ToolTip.text: root.title

                HoverHandler { id: titleHover }
            }
        }

        Label {
            id: metadataLabel
            objectName: "timelineClipMetadataLabel"
            Layout.fillWidth: true
            text: root.metadata
            color: root.selected ? theme.textMain : theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: theme.typeCaption
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 7
            elide: Text.ElideNone
            ToolTip.visible: metadataHover.hovered && contentWidth > width + 1
            ToolTip.delay: 350
            ToolTip.timeout: 3600
            ToolTip.text: root.metadata

            HoverHandler { id: metadataHover }
        }

        Flow {
            id: statusFlow
            objectName: "timelineClipStatusStrip"
            Layout.fillWidth: true
            spacing: theme.space4
            visible: root.statusBadges && root.statusBadges.length > 0
            clip: true

            Repeater {
                model: root.statusBadges ? root.statusBadges : []

                delegate: Item {
                    id: statusBadge
                    required property var modelData

                    readonly property string badgeKey: String(modelData.key || "status")
                    readonly property string badgeLabel: String(modelData.label || "")
                    readonly property string badgeTone: String(modelData.tone || "neutral")
                    readonly property bool linkedBadge: badgeKey === "linked" || badgeKey === "group"
                    readonly property bool linkedTextBadge: badgeKey === "linked"

                    objectName: "timelineClipStatusBadge-" + badgeKey
                    width: statusBadgeRow.implicitWidth
                    height: statusBadgeRow.implicitHeight
                    Accessible.name: String(modelData.tooltip || badgeLabel)
                    Accessible.role: Accessible.StaticText
                    ToolTip.visible: statusHover.hovered
                    ToolTip.delay: 350
                    ToolTip.timeout: 3600
                    ToolTip.text: String(modelData.tooltip || badgeLabel)

                    HoverHandler { id: statusHover }

                    Row {
                        id: statusBadgeRow
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: theme.space2

                        AppIcon {
                            id: statusBadgeIcon
                            visible: String(statusBadge.modelData.iconName || "").length > 0
                            width: theme.scaled(12)
                            height: theme.scaled(12)
                            name: String(statusBadge.modelData.iconName || "info")
                            color: statusBadge.linkedBadge
                                   ? theme.accentCool300
                                   : statusBadge.badgeTone === "danger"
                                   ? theme.error
                                   : statusBadge.badgeTone === "warning"
                                     ? theme.warning
                                     : statusBadge.badgeTone === "ready"
                                       ? theme.success
                                       : theme.textMuted
                        }

                        Label {
                            visible: !statusBadge.linkedTextBadge
                            width: visible ? implicitWidth : 0
                            text: statusBadge.badgeLabel
                            color: statusBadge.linkedBadge
                                   ? theme.accentCool300
                                   : statusBadge.badgeTone === "danger"
                                   ? theme.error
                                   : statusBadge.badgeTone === "warning"
                                     ? theme.warning
                                     : statusBadge.badgeTone === "ready"
                                       ? theme.success
                                       : theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeCaption
                            font.weight: Font.DemiBold
                            fontSizeMode: Text.FixedSize
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideNone
                        }
                    }
                }
            }
        }
    }
}
