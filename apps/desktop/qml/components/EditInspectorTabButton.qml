import QtQuick

Item {
    id: root

    property string label: ""
    property string tabKey: ""
    property bool selected: false
    property color accent: theme.polar

    signal selectedRequested(string tabKey)

    Theme { id: theme }

    readonly property string editInspectorTabButtonContract: "edit-inspector-tab-button:v1|selected-state:true|hover:true|keyboard:true|width-to-label:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-width:true|label-fit:true|tooltip-full:true|accessibility-full:true"

    implicitWidth: Math.max(54, tabLabel.implicitWidth + theme.space20)
    implicitHeight: 26
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTab
    Accessible.name: root.label
    Accessible.selected: root.selected

    Keys.onReturnPressed: root.selectedRequested(root.tabKey)
    Keys.onEnterPressed: root.selectedRequested(root.tabKey)
    Keys.onSpacePressed: root.selectedRequested(root.tabKey)

    Rectangle {
        anchors.fill: parent
        radius: theme.radius6
        color: tabMouse.containsMouse || root.selected || root.activeFocus
               ? theme.alpha(root.accent, root.selected ? 0.07 : 0.04)
               : "transparent"
        border.width: root.activeFocus || root.selected ? 1 : 0
        border.color: root.activeFocus || root.selected ? theme.controlEdge : "transparent"
    }

    Text {
        id: tabLabel
        anchors.centerIn: parent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        text: root.label
        color: root.selected ? theme.textStrong : (tabMouse.containsMouse || root.activeFocus ? theme.textMain : theme.textMuted)
        font.family: theme.fontMono
        font.pixelSize: 10
        font.weight: root.selected ? Font.DemiBold : Font.Normal
        font.letterSpacing: 1.0
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        maximumLineCount: 1
        elide: Text.ElideNone
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 7
    }

    MouseArea {
        id: tabMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.selectedRequested(root.tabKey)
    }

    AppToolTip {
        targetItem: root
        visible: tabMouse.containsMouse && root.label.length > 0
        delay: 350
        timeout: 2200
        text: root.label
    }
}
