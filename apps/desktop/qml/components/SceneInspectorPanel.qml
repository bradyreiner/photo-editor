import QtQuick
import QtQuick.Controls.Basic

Control {
    id: panel
    property var studioHost: null
    property string panelKey: ""
    property bool standaloneOpen: false
    readonly property bool opened: studioHost
        ? studioHost.inspectorVisible && studioHost.activePanel === panelKey : standaloneOpen
    readonly property string surfaceRole: "studio-inspector"
    visible: opened
    padding: theme.panelPadding
    function open(): void {
        if (studioHost) studioHost.showPanel(panelKey)
        else standaloneOpen = true
    }
    function close(): void {
        if (studioHost) {
            if (studioHost.activePanel === panelKey) studioHost.closePanel()
        } else standaloneOpen = false
    }
    Theme { id: theme }
    background: Rectangle { color: theme.ink900 }
}
