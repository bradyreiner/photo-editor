import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string channelName: ""
    property bool selected: false
    property color accent: theme.accentWarm400

    signal selectedRequested(string channelName)

    Theme { id: theme }

    readonly property string toneCurveChannelButtonContract: "tone-curve-channel-button:v1|selected-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true|tooltip-full:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    implicitHeight: 24
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.channelName.length > 0 ? root.channelName + " curve channel" : "Curve channel"
    Accessible.selected: root.selected

    Keys.onReturnPressed: root.selectedRequested(root.channelName)
    Keys.onEnterPressed: root.selectedRequested(root.channelName)
    Keys.onSpacePressed: root.selectedRequested(root.channelName)

    Rectangle {
        anchors.fill: parent
        radius: theme.radius6
        color: root.selected
               ? theme.alpha(root.accent, 0.09)
               : (channelMouse.containsMouse || root.activeFocus
                  ? theme.alpha(theme.glassLift, 0.30)
                  : theme.alpha(theme.ink950, 0.30))
        border.width: root.activeFocus ? 2 : 0
        border.color: root.activeFocus
                      ? theme.focusRing
                      : "transparent"
    }

    Text {
        anchors.centerIn: parent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: theme.space6
        anchors.rightMargin: theme.space6
        text: root.channelName
        color: root.selected ? theme.textStrong : (channelMouse.containsMouse || root.activeFocus ? theme.textMain : theme.textMuted)
        font.family: theme.fontUi
        font.pixelSize: 9
        font.weight: Font.DemiBold
        font.letterSpacing: 0
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        maximumLineCount: 1
        elide: Text.ElideNone
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 7
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        visible: root.selected
        color: root.accent
    }

    MouseArea {
        id: channelMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.selectedRequested(root.channelName)
    }

    ToolTip.visible: channelMouse.containsMouse && root.channelName.length > 0
    ToolTip.delay: 350
    ToolTip.timeout: 2600
    ToolTip.text: root.channelName + " curve channel"
}
