import QtQuick
import QtQuick.Controls.Basic

CheckBox {
    id: root

    property string accessibleLabel: ""
    property string accessibleDescription: ""
    property bool allowLabelWrap: false
    readonly property string resolvedAccessibleName: {
        const explicitName = String(root.accessibleLabel || "").trim()
        if (explicitName.length > 0)
            return explicitName

        const visibleLabel = String(root.text || "").trim()
        if (visibleLabel.length > 0)
            return visibleLabel

        const objectLabel = String(root.objectName || "")
            .replace(/(CheckBox|Checkbox|Toggle|Control)$/i, "")
            .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
            .replace(/[_-]+/g, " ")
            .trim()
        if (objectLabel.length > 0)
            return objectLabel.charAt(0).toUpperCase() + objectLabel.slice(1) + " option"

        return qsTr("Option")
    }
    readonly property string resolvedAccessibleDescription: {
        const explicitDescription = String(root.accessibleDescription || "").trim()
        if (explicitDescription.length > 0)
            return explicitDescription
        return String(root.text || "").trim()
    }
    readonly property string toneCheckBoxContract: "tone-checkbox:v1|shared-component:true|accessible-name-role-state:true|keyboard:true|focus-visible:true|hover:true|readable-label:true|theme:true"

    Theme { id: theme }

    implicitWidth: checkLabel.implicitWidth + leftPadding + rightPadding
    implicitHeight: Math.max(theme.controlHeightDense, checkLabel.implicitHeight + topPadding + bottomPadding)
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    font.weight: theme.weightControl
    spacing: theme.space8
    leftPadding: checkIndicator.width + spacing
    rightPadding: theme.space8
    topPadding: theme.space4
    bottomPadding: theme.space4
    hoverEnabled: true
    activeFocusOnTab: true

    Accessible.name: root.resolvedAccessibleName
    Accessible.description: root.resolvedAccessibleDescription
    Accessible.role: Accessible.CheckBox
    Accessible.checked: root.checked

    indicator: Rectangle {
        id: checkIndicator
        x: root.mirrored ? root.width - root.leftPadding + root.spacing
                         : Math.max(0, root.leftPadding - width - root.spacing)
        y: Math.round((root.height - height) / 2)
        width: theme.scaled(16)
        height: width
        radius: theme.radiusControl
        color: root.checked ? theme.accentWarm400
               : root.enabled && root.hovered ? theme.fieldHoverFill : theme.fieldFill
        border.width: root.visualFocus ? theme.focusBorderWidth : theme.hairlineWidth
        border.color: root.visualFocus ? theme.focusRing
                      : root.checked ? theme.accentWarm400
                      : theme.controlEdge
        Accessible.ignored: true

        AppIcon {
            anchors.centerIn: parent
            width: theme.scaled(11)
            height: width
            name: "check"
            color: theme.ink950
            visible: root.checked
        }
    }

    contentItem: Text {
        id: checkLabel
        text: root.text
        font: root.font
        color: root.enabled ? theme.textStrong : theme.textMuted
        verticalAlignment: Text.AlignVCenter
        wrapMode: root.allowLabelWrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
        maximumLineCount: root.allowLabelWrap ? 2 : 1
        elide: Text.ElideNone
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: theme.typeCaption
    }

    background: Rectangle {
        radius: theme.radiusControl
        color: root.enabled && root.hovered ? theme.editorControlHover : "transparent"
        border.width: root.visualFocus ? theme.focusBorderWidth : 0
        border.color: theme.focusRing
    }
}
