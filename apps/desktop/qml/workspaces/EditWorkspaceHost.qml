import QtQuick

Item {
    id: root
    objectName: "editStudioWorkspace"

    property var catalogAssetModel: catalogAssets
    property string editTool: "adjust"
    property bool workspaceActive: true
    readonly property alias contentWorkspace: editWorkspace
    property alias editSidebarCollapsed: editWorkspace.editSidebarCollapsed
    property alias editContextPanelVisible: editWorkspace.editContextPanelVisible
    property alias editInspectorPanelVisible: editWorkspace.editInspectorPanelVisible
    property alias fullScreenEditingEnabled: editWorkspace.fullScreenEditingEnabled
    property alias focusEditLayoutEnabled: editWorkspace.focusEditLayoutEnabled
    property alias editInspectorTab: editWorkspace.editInspectorTab
    property alias studioPresetDraftName: editWorkspace.studioPresetDraftName
    property alias studioPresetSearchText: editWorkspace.studioPresetSearchText
    readonly property bool editingChromeVisible: editWorkspace.editingChromeVisible
    readonly property int editInspectorPanelWidth: editWorkspace.editInspectorPanelWidth
    readonly property string editWorkflowPolishContract: editWorkspace.editWorkflowPolishContract
    readonly property string editPageCompletionContract: editWorkspace.editPageCompletionContract

    signal assetActivated(string destination)

    function activeVisibilityContract() {
        return editWorkspace.activeVisibilityContract()
    }

    function editWorkflowPolishSignature() {
        return editWorkspace.editWorkflowPolishSignature()
    }

    function canSelectAll() {
        return editWorkspace.canSelectAll()
    }

    function selectAll() {
        editWorkspace.selectAll()
    }

    function canDeselect() {
        return editWorkspace.canDeselect()
    }

    function deselectAll() {
        editWorkspace.deselectAll()
    }

    function activateEditTool(toolName) {
        root.editTool = String(toolName || "adjust")
        editWorkspace.activateEditTool(root.editTool)
    }

    function brushToolReady() {
        return editWorkspace.brushToolReady()
    }

    StudioWorkspace {
        id: editWorkspace
        objectName: "editStudioWorkspaceContent"
        anchors.fill: parent
        catalogAssets: root.catalogAssetModel
        page: "edit"
        editTool: root.editTool
        workspaceActive: root.workspaceActive

        onAssetActivated: function(destination) {
            root.assetActivated(destination)
        }
    }
}
