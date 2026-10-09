import QtQuick
import QtQuick.Controls.Basic

TextField {
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
            .replace(/(TextField|Field|Input|Control)$/i, "")
            .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
            .replace(/[_-]+/g, " ")
            .trim()
        if (objectLabel.length > 0)
            return objectLabel.charAt(0).toUpperCase() + objectLabel.slice(1) + " field"

        return qsTr("Text field")
    }

    Theme { id: theme }

    implicitHeight: theme.controlHeightTall
    color: root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.68)
    placeholderTextColor: theme.textFaint
    selectedTextColor: theme.ink950
    selectionColor: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.85)
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    font.weight: theme.weightControl
    leftPadding: theme.space8
    rightPadding: theme.space8
    activeFocusOnTab: true
    hoverEnabled: true
    Accessible.name: root.resolvedAccessibleName
    Accessible.description: root.placeholderText
    Accessible.role: Accessible.EditableText

    background: Rectangle {
        radius: theme.radiusControl
        color: !root.enabled ? theme.alpha(theme.fieldFill, 0.45)
               : root.hovered ? theme.fieldHoverFill : theme.fieldFill
        border.width: root.activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
        border.color: root.activeFocus ? theme.focusRing
                      : root.hovered ? theme.alpha(theme.controlEdge, 0.82)
                      : theme.alpha(theme.controlEdge, 0.52)
    }
}
