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
    implicitWidth: Math.max(72, Math.min(theme.menuMaxWidth, contentRow.implicitWidth + theme.space16))
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: root.text
    Accessible.description: root.tooltipText
    Accessible.role: Accessible.Button
    Accessible.checked: root.active
    ToolTip.visible: root.hovered && (root.tooltipText.length > 0 ||
                                     (root.showFullTextTooltip &&
                                      contentLabel.contentWidth > contentLabel.width + 1))
    ToolTip.delay: 250
    ToolTip.timeout: 3000
    ToolTip.text: root.tooltipText.length > 0 ? root.tooltipText : root.text

    background: Rectangle {
        radius: 3
        border.width: root.visualFocus ? 2 : 0
        border.color: !root.enabled
                      ? theme.alpha(theme.lineSoft, 0.12)
                      : root.visualFocus
                        ? theme.alpha(theme.focusRing, 0.92)
                        : "transparent"
        color: !root.enabled
               ? theme.alpha(theme.glassBase, 0.4)
               : root.down
                 ? theme.surfaceInset
                 : root.active
                   ? (root.tone === "neutral" ? theme.ink750 : theme.alpha(root.resolvedAccent, 0.12))
                   : root.hovered
                     ? theme.alpha(theme.editorControlHover, 0.64)
                     : "transparent"

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(1, parent.radius - 1)
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, 0.22)
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
            font.weight: root.active ? Font.Bold : Font.Medium
            font.letterSpacing: 0
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize
            minimumPixelSize: 10
            clip: true
        }
    }
}
