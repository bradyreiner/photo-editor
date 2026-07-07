import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string label: ""
    property string iconName: ""
    property string actionKey: ""
    property bool active: false

    signal triggered(string actionKey)

    Theme { id: theme }

    readonly property string editLayoutToggleRowContract: "edit-layout-toggle-row:v1|active-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true|tooltip-full:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    implicitHeight: 28
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.label

    Keys.onReturnPressed: root.triggered(root.actionKey)
    Keys.onEnterPressed: root.triggered(root.actionKey)
    Keys.onSpacePressed: root.triggered(root.actionKey)

    Rectangle {
        anchors.fill: parent
        radius: theme.radius6
        color: root.active
               ? theme.alpha(theme.polar, 0.07)
               : (rowMouse.containsMouse || root.activeFocus
                  ? theme.alpha(theme.polar, 0.045)
                  : "transparent")
        border.width: root.activeFocus ? 2 : 0
        border.color: root.activeFocus
                      ? theme.focusRing
                      : "transparent"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        spacing: theme.space8

        AppIcon {
            name: root.iconName
            color: root.active ? theme.textMain : (rowMouse.containsMouse || root.activeFocus ? theme.textMain : theme.textMuted)
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
        }

        Text {
            Layout.fillWidth: true
            text: root.label
            color: root.active ? theme.textStrong : (rowMouse.containsMouse || root.activeFocus ? theme.textMain : theme.textMuted)
            font.family: theme.fontMono
            font.pixelSize: 8
            font.letterSpacing: 0.55
            font.weight: root.active ? Font.DemiBold : Font.Medium
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 6
            verticalAlignment: Text.AlignVCenter
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.triggered(root.actionKey)
    }

    ToolTip.visible: rowMouse.containsMouse && root.label.length > 0
    ToolTip.delay: 350
    ToolTip.timeout: 2400
    ToolTip.text: root.label
}
