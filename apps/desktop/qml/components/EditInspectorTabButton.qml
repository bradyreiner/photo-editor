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
    readonly property string editInspectorTabVisualContract: "edit-inspector-tab-visual:v3|pressed:true|focus-ring:2px|target:density-aware-control|selection:neutral-fill-and-weight|text-minimum:10px|motion:reduced-motion-aware|corners:2px"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|label-width:true|label-fit:true|minimum-px:10|tooltip-full:true|accessibility-full:true"

    implicitWidth: Math.max(54, tabLabel.implicitWidth + theme.space20)
    implicitHeight: theme.controlHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTab
    Accessible.name: root.label
    Accessible.selected: root.selected

    Keys.onReturnPressed: root.selectedRequested(root.tabKey)
    Keys.onEnterPressed: root.selectedRequested(root.tabKey)
    Keys.onSpacePressed: root.selectedRequested(root.tabKey)

    Rectangle {
        anchors.fill: parent
        radius: theme.scaled(2)
        color: theme.commandFill("secondary", root.selected, tabMouse.containsMouse,
                                 tabMouse.pressed, root.enabled, true)
        border.width: theme.commandBorderWidth(root.activeFocus)
        border.color: root.activeFocus ? theme.focusRing : theme.controlEdge

        Behavior on color {
            ColorAnimation { duration: theme.reducedMotion ? 0 : theme.durationFast; easing.type: Easing.OutCubic }
        }
    }

    Text {
        id: tabLabel
        anchors.centerIn: parent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        text: root.label
        color: root.selected ? theme.textStrong : (tabMouse.pressed || tabMouse.containsMouse || root.activeFocus ? theme.textMain : theme.textMuted)
        font.family: theme.fontMono
        font.pixelSize: theme.typeLabel
        font.weight: root.selected ? Font.DemiBold : Font.Normal
        font.letterSpacing: 0.35
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        maximumLineCount: 1
        elide: Text.ElideNone
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 10
    }

    MouseArea {
        id: tabMouse
        anchors.fill: parent
        enabled: root.enabled
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
