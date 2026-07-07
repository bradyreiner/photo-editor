import QtQuick
import QtQuick.Controls

Button {
    id: root

    property bool active: false
    property color accent: theme.accentWarm400
    property bool fitTextToWidth: true
    property bool showFullTextTooltip: true

    Theme { id: theme }

    implicitHeight: theme.controlHeightDense
    implicitWidth: Math.max(72, Math.min(theme.menuMaxWidth, contentLabel.implicitWidth + theme.space16))
    font.family: theme.fontUi
    font.pixelSize: 12
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: root.text
    ToolTip.visible: root.hovered && root.showFullTextTooltip && contentLabel.contentWidth > contentLabel.width + 1
    ToolTip.delay: 250
    ToolTip.timeout: 3000
    ToolTip.text: root.text

    background: Rectangle {
        radius: theme.radiusControl
        border.width: root.visualFocus ? 2 : 0
        border.color: !root.enabled
                      ? theme.alpha(theme.lineSoft, 0.12)
                      : root.visualFocus
                        ? theme.alpha(theme.focusRing, 0.92)
                        : "transparent"
        color: !root.enabled
               ? theme.alpha(theme.glassBase, 0.4)
               : root.down
                 ? theme.alpha(root.accent, 0.14)
                 : root.active
                   ? theme.alpha(theme.glassLift, 0.36)
                   : root.hovered
                     ? theme.alpha(theme.glassLift, 0.36)
                     : theme.alpha(theme.glassBase, 0.38)

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(1, parent.radius - 1)
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, 0.22)
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: theme.space10
            anchors.rightMargin: theme.space10
            height: 2
            radius: 1
            visible: root.enabled && root.active
            color: root.accent
        }
    }

    contentItem: Text {
        id: contentLabel
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
        minimumPixelSize: 9
        clip: true
    }
}
