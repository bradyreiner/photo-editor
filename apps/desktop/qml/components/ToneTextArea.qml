import QtQuick
import QtQuick.Controls.Basic

TextArea {
    id: root

    property color accent: theme.accentWarm400
    property string accessibleLabel: ""
    readonly property string resolvedAccessibleName: {
        const explicitName = String(root.accessibleLabel || "").trim()
        if (explicitName.length > 0)
            return explicitName

        const placeholder = String(root.placeholderText || "").trim()
        if (placeholder.length > 0)
            return placeholder

        const objectLabel = String(root.objectName || "")
            .replace(/(TextArea|Area|Field|Input|Control)$/i, "")
            .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
            .replace(/[_-]+/g, " ")
            .trim()
        if (objectLabel.length > 0)
            return objectLabel.charAt(0).toUpperCase() + objectLabel.slice(1) + " text area"

        return qsTr("Text area")
    }
    readonly property string toneTextAreaContract: "tone-text-area:v2|shared-component:true|multiline:true|accessible-name:true|read-only-role:true|selection:true|focus:true|disabled:true|theme:true"

    Theme { id: theme }

    implicitHeight: theme.controlHeightPrimary * 2
    color: root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.68)
    placeholderTextColor: theme.textFaint
    selectedTextColor: theme.ink950
    selectionColor: theme.alpha(root.accent, 0.85)
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    font.weight: theme.weightControl
    leftPadding: theme.space10
    rightPadding: theme.space10
    topPadding: theme.space10
    bottomPadding: theme.space10
    activeFocusOnTab: true
    hoverEnabled: true
    selectByMouse: true
    wrapMode: TextEdit.Wrap
    Accessible.name: root.resolvedAccessibleName
    Accessible.description: root.placeholderText
    Accessible.role: root.readOnly ? Accessible.StaticText : Accessible.EditableText

    background: Rectangle {
        radius: theme.radiusControl
        color: root.enabled ? theme.fieldFill : theme.alpha(theme.fieldFill, 0.45)
        border.width: root.activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
        border.color: root.activeFocus ? theme.focusRing
                      : root.hovered ? theme.alpha(theme.controlEdge, 0.82)
                      : theme.alpha(theme.controlEdge, 0.52)

        Rectangle {
            anchors.fill: parent
            anchors.margins: theme.hairlineWidth
            radius: Math.max(1, parent.radius - theme.hairlineWidth)
            visible: root.activeFocus
            color: theme.alpha(theme.focusFill, 0.2)
        }
    }
}
