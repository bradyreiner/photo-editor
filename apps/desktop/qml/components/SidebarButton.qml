import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root

    property color accent: "#c99a52"
    property string kind: "library"
    property string title: ""
    property string subtitle: ""
    property string tooltipText: ""
    property string badgeText: ""
    property bool badgeEmphasis: false
    property bool compact: false

    Theme { id: theme }

    implicitHeight: compact ? 36 : 34
    font.family: theme.fontUi
    font.pixelSize: 12
    hoverEnabled: true

    ToolTip.visible: root.hovered && root.tooltipText.length > 0
    ToolTip.delay: 250
    ToolTip.timeout: 3000
    ToolTip.text: root.tooltipText

    background: Rectangle {
        radius: theme.radius8
        color: root.down
               ? theme.ink800
               : root.highlighted
                 ? theme.alpha(theme.ink800, 0.9)
                 : (root.hovered ? theme.alpha(theme.ink700, 0.22) : "transparent")
        border.width: root.highlighted ? 1 : 0
        border.color: root.highlighted
                      ? theme.alpha(theme.lineStrong, 0.55)
                      : "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 0
            width: 2
            height: parent.height - 10
            radius: 1
            visible: root.highlighted
            color: theme.textMuted
        }

        Behavior on color {
            ColorAnimation { duration: theme.durationFast }
        }
    }

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 8

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
            radius: theme.radius8
            color: root.highlighted ? theme.alpha(theme.lineStrong, 0.34) : "transparent"
            implicitWidth: Math.max(20, badgeLabel.implicitWidth + 10)
            implicitHeight: 20

            Text {
                id: badgeLabel
                anchors.centerIn: parent
                text: root.badgeText
                color: root.highlighted ? theme.textMain : theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: 9
            }
        }
    }
}
