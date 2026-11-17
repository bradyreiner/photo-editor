import QtQuick
import QtQuick.Controls.Basic

CheckBox {
    id: root

    property string accessibleLabel: ""
    property string accessibleDescription: ""
    property string description: ""
    property bool allowLabelWrap: true
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
        return String(root.description || root.text || "").trim()
    }
    readonly property string toneCheckBoxContract: "tone-checkbox:v1|shared-component:true|accessible-name-role-state:true|keyboard:true|focus-visible:true|hover:true|readable-label:true|theme:true"

    Theme { id: theme }
    readonly property string choiceStyleContract: "choice-style:v1|checkbox|shared|neutral|scaled|fixed-text"
    readonly property string choiceKind: "checkbox"
    readonly property real choiceScale: theme.interfaceScale
    readonly property bool choiceHighContrast: theme.highContrast

    implicitWidth: Math.max(checkLabel.implicitWidth, detailLabel.visible ? detailLabel.implicitWidth : 0) + leftPadding + rightPadding
    implicitHeight: Math.max(theme.controlHeightDense, checkContent.implicitHeight + topPadding + bottomPadding)
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
    Accessible.onPressAction: if (root.enabled) root.click()
    Accessible.onToggleAction: if (root.enabled) root.click()

    indicator: Rectangle {
        id: checkIndicator
        x: root.mirrored ? root.width - root.leftPadding + root.spacing
                         : Math.max(0, root.leftPadding - width - root.spacing)
        y: Math.round((root.height - height) / 2)
        width: theme.scaled(16)
        height: width
        radius: theme.radiusControl
        color: root.checkState !== Qt.Unchecked ? theme.textStrong
               : root.enabled && root.hovered ? theme.fieldHoverFill : theme.fieldFill
        border.width: theme.highContrast ? theme.hairlineWidth : 0
        border.color: theme.controlEdge
        Accessible.ignored: true

        AppIcon {
            anchors.centerIn: parent
            width: theme.scaled(11)
            height: width
            name: root.checkState === Qt.PartiallyChecked ? "minus" : "check"
            color: theme.ink950
            visible: root.checkState !== Qt.Unchecked
        }
    }

    contentItem: Column {
        id: checkContent
        spacing: root.description.length > 0 ? theme.space2 : 0
        Text {
            id: checkLabel
            objectName: "sharedChoiceLabel"
            width: parent.width
            text: root.text
            font: root.font
            color: root.enabled ? theme.textStrong : theme.textMuted
            wrapMode: root.allowLabelWrap ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
            elide: Text.ElideNone
            fontSizeMode: Text.FixedSize
        }
        Text {
            id: detailLabel
            objectName: "sharedChoiceCaption"
            width: parent.width
            visible: root.description.length > 0
            text: root.description
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            color: theme.textMuted
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            elide: Text.ElideNone
            fontSizeMode: Text.FixedSize
        }
    }

    background: CommandButtonSurface {
        focused: root.visualFocus
        interactiveEnabled: root.enabled
        hovered: root.hovered
        fillColor: theme.commandFill("secondary", false, root.hovered, root.down, root.enabled, true)
    }
}
