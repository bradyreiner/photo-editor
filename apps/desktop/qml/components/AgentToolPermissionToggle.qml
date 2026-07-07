import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string label: "Tool"
    property string helperText: ""
    property bool checked: false
    property bool toggleEnabled: true

    signal toggled(bool checked)

    readonly property string agentToolPermissionToggleContract: "agent-tool-permission-toggle:v1|checked-state:true|keyboard:true|hover:true|disabled-state:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    Theme { id: theme }

    implicitWidth: Math.max(86, permissionContent.implicitWidth + theme.space16)
    implicitHeight: theme.controlHeightDense
    radius: theme.radiusControl
    color: !toggleEnabled
           ? theme.alpha(theme.glassBase, 0.22)
           : checked
             ? theme.alpha(theme.focusRing, permissionHover.hovered ? 0.18 : 0.12)
             : permissionHover.hovered
               ? theme.alpha(theme.glassLift, 0.38)
               : theme.alpha(theme.glassBase, 0.34)
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
    Accessible.description: helperText.length > 0 ? helperText : label
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
        id: permissionHover
        cursorShape: root.toggleEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
    }

    RowLayout {
        id: permissionContent
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        spacing: theme.space6

        AppIcon {
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            Layout.alignment: Qt.AlignVCenter
            name: root.checked ? "check" : "plus"
            color: root.checked ? theme.focusRing : theme.textMuted
        }

        Text {
            id: permissionLabel
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

    ToolTip.visible: permissionHover.hovered && (helperText.length > 0 || permissionLabel.contentWidth > permissionLabel.width + 1)
    ToolTip.delay: 250
    ToolTip.timeout: 3200
    ToolTip.text: helperText.length > 0 ? helperText : label

    TapHandler {
        enabled: root.toggleEnabled
        onTapped: root.toggle()
    }
}
