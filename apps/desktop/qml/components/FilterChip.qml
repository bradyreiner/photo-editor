import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: root

    property bool active: false
    property color accent: theme.accentWarm400
    property string tone: "neutral"
    property string iconName: ""
    property string tooltipText: ""
    property bool fitTextToWidth: true
    property bool showFullTextTooltip: true
    readonly property color resolvedAccent: tone === "danger" ? theme.error
                                                    : tone === "warning" ? theme.warning
                                                    : tone === "positive" ? theme.success
                                                    : accent
    readonly property string interactionStateContract: theme.interactionStateContract

    Theme { id: theme }

    implicitHeight: theme.controlHeightDense
    implicitWidth: Math.max(52, Math.min(theme.menuMaxWidth, contentRow.implicitWidth + theme.space12))
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: root.text
    Accessible.description: root.tooltipText
    Accessible.role: Accessible.Button
    Accessible.checked: root.active
    background: Rectangle {
        radius: theme.radiusControl
        border.width: theme.commandBorderWidth(root.visualFocus)
        border.color: root.visualFocus ? theme.focusRing : theme.controlEdge
        color: theme.commandFill("secondary", root.active, root.hovered, root.down, root.enabled, false)

        Behavior on color {
            ColorAnimation { duration: theme.reducedMotion ? 0 : theme.durationFast; easing.type: Easing.OutCubic }
        }

    }

    contentItem: RowLayout {
        id: contentRow
        spacing: root.iconName.length > 0 ? theme.space4 : 0

        AppIcon {
            visible: root.iconName.length > 0
            name: root.iconName.length > 0 ? root.iconName : "check"
            Layout.preferredWidth: visible ? theme.scaled(14) : 0
            Layout.preferredHeight: visible ? theme.scaled(14) : 0
            color: !root.enabled ? theme.alpha(theme.textMuted, 0.65)
                                 : (root.active ? theme.textStrong : theme.textMain)
        }

        Text {
            id: contentLabel
            Layout.fillWidth: true
            text: root.text
            color: !root.enabled ? theme.alpha(theme.textMuted, 0.65) : (root.active ? theme.textStrong : theme.textMain)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: root.font.family
            font.pixelSize: root.font.pixelSize
            font.weight: theme.weightControl
            font.letterSpacing: 0
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize
            minimumPixelSize: 10
            clip: true
        }
    }

    AppToolTip {
        targetItem: root
        visible: root.hovered && (root.tooltipText.length > 0 ||
                                  (root.showFullTextTooltip &&
                                   contentLabel.contentWidth > contentLabel.width + 1))
        text: root.tooltipText.length > 0 ? root.tooltipText : root.text
    }
}
