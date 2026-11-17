import QtQuick
import QtQuick.Controls.Basic

Switch {
    id: root
    property string accessibleLabel: text
    property string accessibleDescription: ""
    readonly property string toneSwitchContract: "tone-switch:v1|shared|neutral|keyboard|accessible|scaled|reduced-motion"
    Theme { id: theme }
    readonly property string choiceStyleContract: "choice-style:v1|switch|shared|neutral|scaled|fixed-text"
    readonly property string choiceKind: "switch"
    readonly property real choiceScale: theme.interfaceScale
    readonly property bool choiceHighContrast: theme.highContrast
    implicitWidth: Math.max(theme.scaled(48), implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(theme.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    padding: theme.space4
    leftPadding: theme.scaled(48) + (text.length > 0 ? theme.space8 : 0)
    rightPadding: text.length > 0 ? theme.space4 : 0
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.role: Accessible.CheckBox
    Accessible.name: root.accessibleLabel
    Accessible.description: root.accessibleDescription
    Accessible.checked: root.checked
    Accessible.onPressAction: if (root.enabled) root.click()
    Accessible.onToggleAction: if (root.enabled) root.click()
    indicator: Rectangle {
        x: 0
        y: Math.round((root.height - height) / 2)
        width: theme.scaled(48)
        height: theme.scaled(26)
        radius: height / 2
        color: root.checked ? theme.textStrong : theme.fieldHoverFill
        border.width: theme.highContrast ? theme.hairlineWidth : 0
        border.color: theme.controlEdge
        opacity: root.enabled ? 1 : 0.5
        Rectangle {
            objectName: "sharedSwitchThumb"
            width: theme.scaled(18)
            height: width
            radius: width / 2
            x: theme.space4 + root.visualPosition * (parent.width - width - theme.space8)
            y: Math.round((parent.height - height) / 2)
            color: root.checked ? theme.ink950 : theme.textStrong
            Behavior on x {
                enabled: !theme.reducedMotion && !root.down
                NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }
            }
        }
    }
    contentItem: Text {
        objectName: "sharedChoiceLabel"
        text: root.text
        font: root.font
        color: root.enabled ? theme.textStrong : theme.textMuted
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
        fontSizeMode: Text.FixedSize
    }
    background: CommandButtonSurface {
        focused: root.visualFocus
        interactiveEnabled: root.enabled
        fillColor: "transparent"
    }
}
