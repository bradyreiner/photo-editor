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

    readonly property bool clippedShape: false
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

    implicitHeight: iconOnly ? theme.railButtonSize : Math.max(compact ? theme.sidebarRowHeightCompact : theme.sidebarRowHeight, titleLabel.implicitHeight + theme.space12)
    implicitWidth: iconOnly ? theme.railButtonSize : Math.max(implicitBackgroundWidth, titleLabel.implicitWidth + (root.badgeText.length > 0 ? badgeLabel.implicitWidth + theme.scaled(54) : theme.scaled(46)))
    font.family: theme.fontUi
    font.pixelSize: compact ? theme.typeLabel : theme.typeControl
    font.weight: theme.weightControl
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: root.tooltipText.length > 0 ? root.tooltipText : (root.title.length > 0 ? root.title : root.kind)
    Accessible.description: root.subtitle
    Accessible.checked: root.highlighted

    AppToolTip {
        id: sharedTooltip1
        targetItem: root
        visible: root.visible && (root.hovered && (root.tooltipText.length > 0 || (root.showFullTextTooltip && titleLabel.visible && (titleLabel.truncated || titleLabel.contentWidth > titleLabel.width + 1))))
        delay: 250
        timeout: 3000
        text: root.tooltipText.length > 0 ? root.tooltipText : root.title
    }
    background: CommandButtonSurface {
        hovered: root.hovered
        pressed: root.down
        selected: root.highlighted
        focused: root.visualFocus
        interactiveEnabled: root.enabled
        fillColor: theme.commandFill("secondary", root.highlighted, root.hovered, root.down, root.enabled, true)
    }

    contentItem: Item {
        anchors.fill: parent

        AppIcon {
                objectName: "sharedCommandIcon"
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
            implicitWidth: theme.scaled(8)
            implicitHeight: theme.scaled(8)
            radius: theme.scaled(4)
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
                objectName: "sharedCommandIcon"
                    anchors.centerIn: parent
                    name: root.iconName
                    color: root.highlighted ? theme.interaction300 : (root.hovered ? theme.textMain : theme.textMuted)
                    implicitWidth: theme.scaled(18)
                    implicitHeight: implicitWidth
                }
            }

            Text {
                id: titleLabel
                objectName: "sharedCommandLabel"
                Layout.fillWidth: true
                text: root.title
                color: root.highlighted ? theme.interaction300 : (root.hovered ? theme.textMain : theme.textMuted)
                font.family: root.font.family
                font.pixelSize: root.font.pixelSize
                font.weight: theme.weightControl
                Layout.alignment: Qt.AlignVCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideNone
                fontSizeMode: Text.FixedSize
                wrapMode: Text.Wrap
            }

            Rectangle {
                visible: root.badgeText.length > 0
                Layout.alignment: Qt.AlignVCenter
                radius: 1
                color: root.badgeEmphasis ? theme.alpha(root.accent, 0.18) : theme.alpha(theme.ink700, 0.5)
                border.width: theme.highContrast ? theme.hairlineWidth : 0
                border.color: root.badgeEmphasis ? theme.alpha(root.accent, 0.34) : theme.alpha(theme.lineSoft, 0.4)
                implicitWidth: badgeLabel.implicitWidth + theme.space8
                implicitHeight: theme.scaled(16)

                Label {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: root.badgeText
                    color: root.badgeEmphasis ? root.accent : theme.textMuted
                    font.family: theme.fontNumeric
                    font.features: theme.numericFontFeatures
                    font.pixelSize: theme.typeMicro
                }
            }
        }
    }
}
