import QtQml

QtObject {
    id: root

    property string page: "edit"
    property bool workspaceActive: false
    property bool dockVisible: false
    property bool largeScopesMode: false
    property string dockPosition: "bottom"
    property bool contextPanelVisible: false
    property bool inspectorPanelVisible: false
    property string inspectorTab: ""

    readonly property bool scopesVisible: workspaceActive && page === "edit" && (
        (dockVisible && largeScopesMode) ||
        (dockVisible && !largeScopesMode && (
            (dockPosition === "left" && contextPanelVisible) ||
            dockPosition === "bottom"
        )) ||
        (dockPosition === "right" && inspectorTab === "scopes" && inspectorPanelVisible)
    )
}
