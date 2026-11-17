pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    required property var dockState
    property var panelExposedProvider: null
    required property var theme
    property int horizontalSpacing: theme.gapDense
    signal panelRequested(string panelId)
    signal resetLayoutRequested()

    readonly property string timelinePanelStripContract:
        "timeline-panel-strip:v3|discoverability:labeled-panels-menu|selection:focus-without-closing|accessibility:panel-state-names|style:shared-menu-button-theme"
    implicitHeight: panelsMenu.implicitHeight
    implicitWidth: panelsMenu.implicitWidth
    Accessible.name: "Timeline panels"

    MenuButton {
        id: panelsMenu
        objectName: "timelinePanelsMenuButton"
        anchors.fill: parent
        text: qsTr("Panels")
        compact: true
        primaryOpensMenu: true
        tooltipText: qsTr("Show or focus a Timeline panel. Drag panel tabs to rearrange your workspace.")
        menuWidth: 240
        menuItems: {
            if (!root.dockState)
                return []
            root.dockState.panelState
            return root.dockState.panelDefinitions.map(function(panel) {
                return { key: panel.id, label: panel.label, checked: root.dockState.visible(panel.id) }
            }).concat([{key: "reset-layout", label: qsTr("Reset panel layout"), icon: "reset", separatorBefore: true}])
        }
        onMenuItemTriggered: function(key) {
            if (key === "reset-layout") {
                root.resetLayoutRequested()
                return
            }
            root.dockState.focusPanel(key)
            root.panelRequested(key)
        }
    }
}
