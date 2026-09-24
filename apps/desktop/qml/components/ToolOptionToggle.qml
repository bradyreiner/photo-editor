import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string label: "Option"
    property string helperText: ""
    property string disabledReason: ""
    property string disabledFallbackReason: "Unavailable in the current state."
    property bool checked: false
    property bool toggleEnabled: true
    property color accent: theme.focusRing

    signal toggled(bool checked)

    readonly property string disabledTooltipText: !toggleEnabled
                                                  ? (disabledReason.length > 0 ? disabledReason : disabledFallbackReason)
                                                  : (helperText.length > 0 ? helperText : label)
    readonly property string toolOptionToggleContract: "tool-option-toggle:v2|checked-state:true|keyboard:true|hover:true|disabled-state:true|disabled-fallback:true|readable-text:true|no-elide:true|tooltip-full:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    Theme { id: theme }

    implicitWidth: Math.max(92, optionContent.implicitWidth + theme.space16)
    implicitHeight: theme.controlHeightDense
    radius: theme.radiusControl
    color: toggleEnabled && optionHover.hovered ? theme.ink850 : "transparent"
    border.width: activeFocus ? 2 : 0
    border.color: !toggleEnabled
                  ? theme.alpha(theme.lineSoft, 0.1)
                  : activeFocus
                    ? theme.alpha(theme.focusRing, 0.9)
                    : "transparent"
    opacity: toggleEnabled ? 1 : 0.58
    activeFocusOnTab: true
    clip: true
    Accessible.name: label
    Accessible.description: disabledTooltipText
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked

    function toggle() {
        if (!toggleEnabled)
            return
        toggled(!checked)
    }

    Keys.onReturnPressed: root.toggle()
    Keys.onEnterPressed: root.toggle()
    Keys.onSpacePressed: root.toggle()

    HoverHandler {
        id: optionHover
        cursorShape: root.toggleEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
    }

    RowLayout {
        id: optionContent
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        spacing: theme.space6

        Rectangle {
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            Layout.alignment: Qt.AlignVCenter
            radius: 2
            color: root.checked ? theme.textStrong : "transparent"
            border.width: 1
            border.color: root.checked ? "transparent" : theme.textFaint

            AppIcon {
                anchors.centerIn: parent
                width: 9
                height: 9
                visible: root.checked
                name: "check"
                color: theme.ink950
            }
        }

        Text {
            id: optionLabel
            Layout.fillWidth: true
            text: root.label
            color: root.checked ? theme.textStrong : theme.textMain
            font.family: theme.fontUi
            font.pixelSize: 11
            font.weight: root.checked ? Font.DemiBold : Font.Medium
            font.letterSpacing: 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 9
            maximumLineCount: 1
            elide: Text.ElideNone
        }
    }

    ToolTip.visible: optionHover.hovered && (disabledTooltipText.length > 0 || optionLabel.contentWidth > optionLabel.width + 1)
    ToolTip.delay: 250
    ToolTip.timeout: 3200
    ToolTip.text: disabledTooltipText

    TapHandler {
        enabled: root.toggleEnabled
        onTapped: root.toggle()
    }
}
