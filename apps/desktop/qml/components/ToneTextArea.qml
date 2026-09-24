import QtQuick
import QtQuick.Controls.Basic

TextArea {
    id: root

    property color accent: theme.accentWarm400
    readonly property string toneTextAreaContract: "tone-text-area:v1|shared-component:true|multiline:true|selection:true|focus:true|disabled:true|theme:true"

    Theme { id: theme }

    implicitHeight: theme.controlHeightPrimary * 2
    color: root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.68)
    placeholderTextColor: theme.textFaint
    selectedTextColor: theme.ink950
    selectionColor: theme.alpha(root.accent, 0.85)
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    leftPadding: theme.space12
    rightPadding: theme.space12
    topPadding: theme.space10
    bottomPadding: theme.space10
    activeFocusOnTab: true
    selectByMouse: true
    wrapMode: TextEdit.Wrap

    background: Rectangle {
        radius: theme.radiusControl
        color: root.enabled ? theme.alpha(theme.glassBase, 0.38) : theme.alpha(theme.glassBase, 0.26)
        border.width: root.activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
        border.color: root.activeFocus ? theme.alpha(theme.focusRing, 0.9) : theme.alpha(theme.glassEdge, 0.2)

        Rectangle {
            anchors.fill: parent
            anchors.margins: theme.hairlineWidth
            radius: Math.max(1, parent.radius - theme.hairlineWidth)
            visible: root.activeFocus
            color: theme.alpha(theme.focusFill, 0.2)
        }
    }
}
