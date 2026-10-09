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
                                            ? theme.ink750
                                          : root.highlighted
                                            ? theme.editorSelectionFill
                                            : (root.hovered ? theme.alpha(theme.glassLift, 0.30) : theme.alpha(theme.ink950, 0.50))
    readonly property color iconOnlyBorder: root.visualFocus
                                           ? theme.focusRing
                                           : root.highlighted
                                               ? "transparent"
                                               : "transparent"

    implicitHeight: iconOnly ? theme.railButtonSize : (compact ? theme.sidebarRowHeightCompact : theme.sidebarRowHeight)
    implicitWidth: iconOnly ? theme.railButtonSize : Math.max(implicitBackgroundWidth, titleLabel.implicitWidth + (root.badgeText.length > 0 ? badgeLabel.implicitWidth + 54 : 46))
    font.family: theme.fontUi
    font.pixelSize: compact ? theme.typeLabel : theme.typeControl
    font.weight: root.highlighted ? theme.weightSection : theme.weightControl
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
                   ? theme.ink750
                   : root.highlighted
                     ? theme.editorSelectionFill
                     : (root.hovered ? theme.editorControlHover : "transparent")
            border.width: root.visualFocus ? theme.focusBorderWidth : 0
            border.color: root.visualFocus
                          ? theme.focusRing
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
                         ? theme.ink750
                         : root.highlighted
                           ? theme.editorSelectionFill
                           : (root.hovered ? theme.editorControlHover : "transparent")
            borderWidth: root.visualFocus ? theme.focusBorderWidth : 0
            borderColor: root.iconOnly
                         ? root.iconOnlyBorder
                         : root.visualFocus
                           ? theme.focusRing
                           : "transparent"
        }

    }

    contentItem: Item {
        anchors.fill: parent

        AppIcon {
            visible: root.iconOnly
            anchors.centerIn: parent
            name: root.iconName
            color: root.highlighted ? theme.interaction300 : (root.hovered ? theme.textMain : theme.textMuted)
            implicitWidth: theme.scaled(18)
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
                Layout.preferredWidth: theme.scaled(18)
                Layout.preferredHeight: theme.scaled(18)
                Layout.alignment: Qt.AlignVCenter

                AppIcon {
                    anchors.centerIn: parent
                    name: root.iconName
                    color: root.highlighted ? theme.interaction300 : (root.hovered ? theme.textMain : theme.textMuted)
                    implicitWidth: theme.scaled(18)
                    implicitHeight: implicitWidth
                }
            }

            Text {
                id: titleLabel
                Layout.fillWidth: true
                text: root.title
                color: root.highlighted ? theme.interaction300 : (root.hovered ? theme.textMain : theme.textMuted)
                font.family: root.font.family
                font.pixelSize: root.font.pixelSize
                font.weight: root.highlighted ? theme.weightSection : theme.weightControl
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
                    font.pixelSize: theme.typeMicro
                }
            }
        }
    }
}
