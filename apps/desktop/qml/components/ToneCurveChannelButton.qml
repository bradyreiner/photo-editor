import QtQuick

Item {
    id: root

    property string channelName: ""
    property bool selected: false
    property color accent: theme.accentWarm400

    signal selectedRequested(string channelName)

    Theme { id: theme }

    readonly property string toneCurveChannelButtonContract: "tone-curve-channel-button:v1|selected-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true|tooltip-full:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    implicitHeight: 34
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.channelName.length > 0 ? root.channelName + " curve channel" : "Curve channel"
    Accessible.selected: root.selected

    Keys.onReturnPressed: root.selectedRequested(root.channelName)
    Keys.onEnterPressed: root.selectedRequested(root.channelName)
    Keys.onSpacePressed: root.selectedRequested(root.channelName)

    Rectangle {
        anchors.fill: parent
        radius: theme.radius8
        color: root.selected
               ? theme.editorSelectionFill
               : (channelMouse.containsMouse || root.activeFocus
                  ? theme.alpha(theme.glassLift, 0.42)
                  : theme.alpha(theme.glassBase, 0.54))
        border.width: root.activeFocus || root.selected ? 1 : 0
        border.color: root.activeFocus || root.selected ? theme.controlEdge
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
        font.pixelSize: theme.typeControl
        font.weight: Font.DemiBold
        font.letterSpacing: 0
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        maximumLineCount: 1
        elide: Text.ElideNone
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 9
    }

    MouseArea {
        id: channelMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.selectedRequested(root.channelName)
    }

    AppToolTip {
        targetItem: root
        visible: channelMouse.containsMouse && root.channelName.length > 0
        delay: 350
        timeout: 2600
        text: root.channelName + " curve channel"
    }
}
