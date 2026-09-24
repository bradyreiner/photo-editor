pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property string title: "Clip"
    property string metadata: ""
    property var statusBadges: []
    property real horizontalInset: 0
    property bool selected: false
    readonly property string timelineClipContentContract: "timeline-clip-content:v2|title-fit:true|metadata-fit:true|status-strip:compact-consistent|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|title-fit:true|metadata-fit:true|status-fit:true|tooltip-full:true|accessibility-full:true"

    Theme { id: theme }

    clip: true
    Accessible.name: title
    Accessible.description: metadata
    Accessible.role: Accessible.StaticText

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space8 + root.horizontalInset
        anchors.rightMargin: theme.space8 + root.horizontalInset
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6
        spacing: theme.space2

        Label {
            id: titleLabel
            objectName: "timelineClipTitleLabel"
            Layout.fillWidth: true
            text: root.title
            color: theme.textStrong
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

        Label {
            id: metadataLabel
            objectName: "timelineClipMetadataLabel"
            Layout.fillWidth: true
            text: root.metadata
            color: theme.textMuted
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

                delegate: Rectangle {
                    id: statusBadge
                    required property var modelData

                    readonly property string badgeKey: String(modelData.key || "status")
                    readonly property string badgeLabel: String(modelData.label || "")
                    readonly property string badgeTone: String(modelData.tone || "neutral")

                    objectName: "timelineClipStatusBadge-" + badgeKey
                    width: Math.min(statusBadgeRow.implicitWidth + theme.space8,
                                    Math.max(38, root.width - root.horizontalInset * 2 - theme.space16))
                    height: statusBadgeRow.implicitHeight + theme.space4
                    radius: theme.radiusChip
                    color: badgeTone === "danger"
                           ? theme.alpha(theme.error, 0.22)
                           : badgeTone === "warning"
                             ? theme.alpha(theme.warning, 0.18)
                             : badgeTone === "ready"
                               ? theme.alpha(theme.success, 0.16)
                               : theme.alpha(theme.ink950, 0.66)
                    border.width: 0
                    Accessible.name: String(modelData.tooltip || badgeLabel)
                    Accessible.role: Accessible.StaticText
                    ToolTip.visible: statusHover.hovered
                    ToolTip.delay: 350
                    ToolTip.timeout: 3600
                    ToolTip.text: String(modelData.tooltip || badgeLabel)

                    HoverHandler { id: statusHover }

                    Row {
                        id: statusBadgeRow
                        anchors.centerIn: parent
                        spacing: theme.space2

                        AppIcon {
                            id: statusBadgeIcon
                            visible: String(statusBadge.modelData.iconName || "").length > 0
                            width: theme.space10
                            height: theme.space10
                            name: String(statusBadge.modelData.iconName || "info")
                            color: statusBadge.badgeTone === "danger"
                                   ? theme.error
                                   : statusBadge.badgeTone === "warning"
                                     ? theme.warning
                                     : statusBadge.badgeTone === "ready"
                                       ? theme.success
                                       : theme.textMuted
                        }

                        Label {
                            width: Math.min(implicitWidth,
                                            Math.max(18, statusBadge.width
                                                     - theme.space8
                                                     - (statusBadgeIcon.visible
                                                        ? theme.space12 : 0)))
                            text: statusBadge.badgeLabel
                            color: statusBadge.badgeTone === "danger"
                                   ? theme.error
                                   : statusBadge.badgeTone === "warning"
                                     ? theme.warning
                                     : theme.textMain
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeMicro
                            font.weight: Font.DemiBold
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 6
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideNone
                        }
                    }
                }
            }
        }
    }
}
