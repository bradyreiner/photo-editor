import QtQuick

ActionButton {
    id: root
    property bool selected: false
    property bool underlineSelection: false
    Theme { id: theme }
    embedded: true
    active: root.selected && !root.underlineSelection
    implicitWidth: Math.max(theme.scaled(54), commandNaturalWidth)
    implicitHeight: theme.controlHeight
    Accessible.role: Accessible.PageTab
    Accessible.selected: root.selected
    Accessible.onPressAction: if (root.enabled) root.click()
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    Rectangle {
        parent: root
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(0, parent.width - theme.space16)
        height: theme.scaled(2)
        visible: root.selected && root.underlineSelection
        color: theme.textStrong
    }
}
