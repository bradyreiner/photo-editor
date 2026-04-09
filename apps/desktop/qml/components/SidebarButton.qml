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
    property bool iconOnly: false

    Theme { id: theme }

    readonly property color iconOnlyFill: root.down
                                         ? theme.ink700
                                         : root.highlighted
                                           ? theme.ink750
                                           : (root.hovered ? theme.alpha(theme.ink800, 0.98) : theme.alpha(theme.ink900, 0.7))
    readonly property color iconOnlyBorder: root.visualFocus
                                           ? theme.alpha(theme.focusRing, 0.9)
                                           : root.highlighted
                                             ? theme.alpha(theme.lineStrong, 0.74)
                                             : (root.hovered ? theme.alpha(theme.lineSoft, 0.46) : theme.alpha(theme.lineSoft, 0.22))

    implicitHeight: iconOnly ? 40 : (compact ? theme.controlHeight : theme.controlHeightTall)
    implicitWidth: iconOnly ? 40 : implicitBackgroundWidth
    font.family: theme.fontUi
    font.pixelSize: compact ? 11 : 12
    hoverEnabled: true
    activeFocusOnTab: true

    ToolTip.visible: root.hovered && root.tooltipText.length > 0
    ToolTip.delay: 250
    ToolTip.timeout: 3000
    ToolTip.text: root.tooltipText

    background: Rectangle {
        radius: root.iconOnly ? theme.radius8 : (theme.radiusControl + 1)
        color: root.iconOnly
               ? root.iconOnlyFill
               : root.down
                 ? theme.alpha(theme.ink750, 0.96)
                 : root.highlighted
                   ? theme.alpha(root.accent, 0.12)
                   : (root.hovered ? theme.alpha(theme.ink700, 0.24) : "transparent")
        border.width: root.visualFocus ? 2 : 1
        border.color: root.iconOnly
                      ? root.iconOnlyBorder
                      : root.visualFocus
                        ? theme.alpha(theme.focusRing, 0.9)
                        : root.highlighted
                          ? theme.alpha(root.accent, 0.34)
                          : (root.hovered ? theme.alpha(theme.lineStrong, 0.16) : "transparent")

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            visible: !root.iconOnly && (root.highlighted || root.hovered)
            color: root.highlighted
                   ? theme.alpha(theme.ink950, 0.14)
                   : theme.alpha(theme.ink950, 0.05)
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(1, parent.radius - 1)
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, 0.18)
        }

        Behavior on color {
            ColorAnimation { duration: theme.durationFast }
        }

        Behavior on border.color {
            ColorAnimation { duration: theme.durationFast }
        }
    }

    contentItem: Item {
        anchors.fill: parent

        AppIcon {
            visible: root.iconOnly
            anchors.centerIn: parent
            name: root.iconName
            color: root.highlighted ? theme.textStrong : (root.hovered ? theme.textMain : theme.textMuted)
            implicitWidth: 16
            implicitHeight: implicitWidth
        }

        Rectangle {
            visible: root.iconOnly && root.badgeText.length > 0
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 4
            anchors.rightMargin: 4
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: root.badgeEmphasis ? theme.alpha(root.accent, 0.9) : theme.alpha(theme.textMuted, 0.85)
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: theme.space12
            anchors.rightMargin: theme.space10
            spacing: theme.gapDense + 1
            visible: !root.iconOnly

            Item {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                Layout.alignment: Qt.AlignVCenter

                AppIcon {
                    anchors.centerIn: parent
                    name: root.iconName
                    color: root.highlighted ? theme.textStrong : (root.hovered ? theme.textMain : theme.textFaint)
                    implicitWidth: 14
                    implicitHeight: implicitWidth
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.title
                color: root.highlighted ? theme.textStrong : (root.hovered ? theme.textMain : theme.textMuted)
                font.family: root.font.family
                font.pixelSize: root.font.pixelSize
                font.weight: root.highlighted ? Font.DemiBold : Font.Medium
                Layout.alignment: Qt.AlignVCenter
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
}
