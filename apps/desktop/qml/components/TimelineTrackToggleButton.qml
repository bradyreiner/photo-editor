import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string label: ""
    property string shortLabel: ""
    property string iconName: ""
    property color checkedColor: theme.accentWarm500
    property bool checked: false

    signal toggled()

    Theme { id: theme }

    readonly property string timelineTrackToggleButtonContract: "timeline-track-toggle-button:v1|checked-state:true|disabled-state:true|hover:true|keyboard:true|tooltip-full:true|accessibility-full:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|compact-glyph:true|tooltip-full:true|accessibility-full:true"

    implicitWidth: 20
    implicitHeight: 18
    activeFocusOnTab: enabled
    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.checked: root.checked

    Keys.onReturnPressed: if (root.enabled) root.toggled()
    Keys.onEnterPressed: if (root.enabled) root.toggled()
    Keys.onSpacePressed: if (root.enabled) root.toggled()

    Rectangle {
        anchors.fill: parent
        radius: theme.radius6
        color: !root.enabled
               ? theme.alpha(theme.ink700, 0.42)
               : root.checked
                 ? theme.alpha(root.checkedColor, 0.24)
                 : (toggleMouse.containsMouse || root.activeFocus
                    ? theme.alpha(theme.glassLift, 0.32)
                    : theme.alpha(theme.ink700, 0.84))
        border.width: root.activeFocus ? 2 : 0
        border.color: root.activeFocus
                      ? theme.focusRing
                      : "transparent"
    }

    AppIcon {
        anchors.centerIn: parent
        visible: root.iconName.length > 0
        name: root.iconName
        color: !root.enabled
               ? theme.alpha(theme.textMuted, 0.52)
               : (root.checked ? theme.textStrong : theme.textMain)
        implicitWidth: 11
        implicitHeight: 11
    }

    Text {
        anchors.centerIn: parent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        visible: root.iconName.length === 0
        text: root.shortLabel
        color: !root.enabled
               ? theme.alpha(theme.textMuted, 0.52)
               : (root.checked ? theme.textStrong : theme.textMain)
        font.family: theme.fontMono
        font.pixelSize: 9
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        maximumLineCount: 1
        elide: Text.ElideNone
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 7
    }

    MouseArea {
        id: toggleMouse
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled()
    }

    ToolTip.visible: toggleMouse.containsMouse && root.label.length > 0
    ToolTip.delay: 350
    ToolTip.timeout: 2600
    ToolTip.text: root.label
}
