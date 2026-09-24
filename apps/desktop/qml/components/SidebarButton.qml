import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: root

    property color accent: theme.accentWarm400
    property string kind: "library"
    property string iconName: kind
    property string title: ""
    property string subtitle: ""
    property string tooltipText: ""
    property string badgeText: ""
    property bool badgeEmphasis: false
    property bool compact: false
    property bool iconOnly: false
    property bool showFullTextTooltip: true
    property string shape: "auto" // auto, standard, clipped

    Theme { id: theme }

    readonly property bool clippedShape: root.shape === "clipped"
    readonly property color iconOnlyFill: root.down
                                            ? theme.alpha(theme.glassLift, 0.40)
                                          : root.highlighted
                                            ? theme.alpha(theme.glassLift, 0.34)
                                            : (root.hovered ? theme.alpha(theme.glassLift, 0.30) : theme.alpha(theme.ink950, 0.50))
    readonly property color iconOnlyBorder: root.visualFocus
                                           ? theme.alpha(theme.focusRing, 0.9)
                                           : root.highlighted
                                               ? "transparent"
                                               : "transparent"

    implicitHeight: iconOnly ? theme.railButtonSize : (compact ? theme.sidebarRowHeightCompact : theme.sidebarRowHeight)
    implicitWidth: iconOnly ? theme.railButtonSize : Math.max(implicitBackgroundWidth, titleLabel.implicitWidth + (root.badgeText.length > 0 ? badgeLabel.implicitWidth + 54 : 46))
    font.family: theme.fontUi
    font.pixelSize: compact ? 11 : 12
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: root.tooltipText.length > 0 ? root.tooltipText : (root.title.length > 0 ? root.title : root.kind)
    Accessible.description: root.subtitle

    ToolTip.visible: root.hovered && (root.tooltipText.length > 0 || (root.showFullTextTooltip && titleLabel.visible && (titleLabel.truncated || titleLabel.contentWidth > titleLabel.width + 1)))
    ToolTip.delay: 250
    ToolTip.timeout: 3000
    ToolTip.text: root.tooltipText.length > 0 ? root.tooltipText : root.title

    background: Item {
        Rectangle {
            anchors.fill: parent
            visible: !root.clippedShape
            radius: theme.radiusControl
            color: root.down
                   ? theme.alpha(theme.ink750, 0.96)
                   : root.highlighted
                     ? theme.alpha(theme.glassLift, 0.34)
                     : (root.hovered ? theme.alpha(theme.ink700, 0.24) : "transparent")
            border.width: root.visualFocus ? 2 : 0
            border.color: root.visualFocus
                          ? theme.alpha(theme.focusRing, 0.9)
                          : root.highlighted
                            ? "transparent"
                            : "transparent"
        }

        CutCornerSurface {
            anchors.fill: parent
            visible: root.clippedShape
            cut: root.iconOnly ? 8 : theme.cutCorner
            cutTopLeft: false
            cutTopRight: false
            cutBottomRight: true
            cutBottomLeft: false
            fillColor: root.iconOnly
                       ? root.iconOnlyFill
                       : root.down
                         ? theme.alpha(theme.ink750, 0.96)
                         : root.highlighted
                           ? theme.alpha(theme.glassLift, 0.34)
                           : (root.hovered ? theme.alpha(theme.ink700, 0.24) : "transparent")
            borderWidth: root.visualFocus ? 2 : 0
            borderColor: root.iconOnly
                         ? root.iconOnlyBorder
                         : root.visualFocus
                           ? theme.alpha(theme.focusRing, 0.9)
                           : root.highlighted
                              ? "transparent"
                              : "transparent"
        }

        Rectangle {
            width: root.iconOnly ? 3 : 4
            height: root.iconOnly ? 15 : 18
            anchors.right: parent.right
            anchors.rightMargin: root.iconOnly ? 5 : 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.iconOnly ? 5 : 5
            color: root.accent
            opacity: root.enabled && root.shape === "clipped" ? (root.highlighted ? 1.0 : (root.hovered ? 0.74 : 0.42)) : 0
            rotation: 34
            transformOrigin: Item.Center

            Behavior on opacity {
                NumberAnimation { duration: theme.durationFast }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(1, theme.radiusControl - 1)
            visible: !root.iconOnly && (root.highlighted || root.hovered)
            color: root.highlighted
                   ? theme.alpha(root.accent, 0.08)
                   : theme.alpha(theme.polar, 0.045)
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 1
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, 0.18)
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
                id: titleLabel
                Layout.fillWidth: true
                text: root.title
                color: root.highlighted ? theme.textStrong : (root.hovered ? theme.textMain : theme.textMuted)
                font.family: root.font.family
                font.pixelSize: root.font.pixelSize
                font.weight: root.highlighted ? Font.DemiBold : Font.Medium
                Layout.alignment: Qt.AlignVCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideNone
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 9
                maximumLineCount: 1
            }

            Rectangle {
                visible: root.badgeText.length > 0
                Layout.alignment: Qt.AlignVCenter
                radius: 1
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
