import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root

    property color accent: "#c99a52"
    property string kind: "library"
    property string iconName: kind
    property string title: ""
    property string subtitle: ""
    property string tooltipText: ""
    property string badgeText: ""
    property bool badgeEmphasis: false
    property bool compact: false

    Theme { id: theme }

    implicitHeight: compact ? theme.controlHeight : theme.controlHeightTall
    font.family: theme.fontUi
    font.pixelSize: compact ? 11 : 12
    hoverEnabled: true

    ToolTip.visible: root.hovered && root.tooltipText.length > 0
    ToolTip.delay: 250
    ToolTip.timeout: 3000
    ToolTip.text: root.tooltipText

    background: Rectangle {
        radius: theme.radiusControl
        color: root.down
               ? theme.ink800
               : root.highlighted
                 ? theme.alpha(theme.ink800, 0.96)
                 : (root.hovered ? theme.alpha(theme.ink700, 0.32) : "transparent")
        border.width: 1
        border.color: root.highlighted
                      ? theme.alpha(root.accent, 0.42)
                      : (root.hovered ? theme.alpha(theme.lineStrong, 0.36) : "transparent")

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 0
            width: 2
            height: parent.height - 8
            radius: 1
            visible: root.highlighted
            color: root.accent
        }

        Behavior on color {
            ColorAnimation { duration: theme.durationFast }
        }

        Behavior on border.color {
            ColorAnimation { duration: theme.durationFast }
        }
    }

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space10
        anchors.rightMargin: theme.space10
        spacing: theme.gapControl

        Rectangle {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            radius: theme.radiusChip
            color: root.highlighted
                   ? theme.alpha(root.accent, 0.18)
                   : (root.hovered ? theme.alpha(theme.ink700, 0.56) : theme.alpha(theme.ink900, 0.7))
            border.width: root.highlighted ? 1 : 0
            border.color: root.highlighted ? theme.alpha(root.accent, 0.36) : "transparent"

            AppIcon {
                anchors.centerIn: parent
                name: root.iconName
                color: root.highlighted ? root.accent : (root.hovered ? theme.textMain : theme.textMuted)
                width: 11
                height: 11
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.title
            color: root.highlighted ? theme.textStrong : (root.hovered ? theme.textMain : theme.textMuted)
            font.family: root.font.family
            font.pixelSize: root.font.pixelSize
            font.weight: root.highlighted ? Font.DemiBold : Font.Medium
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Rectangle {
            visible: root.badgeText.length > 0
            Layout.alignment: Qt.AlignVCenter
            radius: theme.radius4
            color: root.badgeEmphasis ? theme.alpha(root.accent, 0.18) : theme.alpha(theme.ink700, 0.5)
            border.width: 1
            border.color: root.badgeEmphasis ? theme.alpha(root.accent, 0.34) : theme.alpha(theme.lineSoft, 0.4)
            implicitWidth: badgeLabel.implicitWidth + 8
            implicitHeight: 16

            Label {
                id: badgeLabel
                anchors.centerIn: parent
                text: root.badgeText
                color: root.badgeEmphasis ? root.accent : theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 8
            }
        }
    }
}
