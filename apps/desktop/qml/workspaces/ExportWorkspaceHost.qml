import QtQuick

Item {
    id: root
    objectName: "exportStudioWorkspace"

    property var catalogAssetModel: catalogAssets
    readonly property string exportWorkflowPolishContract: exportWorkspace.exportWorkflowPolishContract
    readonly property string exportPageCompletionContract: exportWorkspace.exportPageCompletionContract

    signal presetManagementRequested(string action)
    signal assetActivated(string destination)

    function activeVisibilityContract() {
        return exportWorkspace.activeVisibilityContract()
    }

    function exportWorkflowPolishSignature() {
        return exportWorkspace.exportWorkflowPolishSignature()
    }

    ExportView {
        id: exportWorkspace
        objectName: "exportStudioWorkspaceContent"
        anchors.fill: parent
        onPresetManagementRequested: function(action) {
            root.presetManagementRequested(action)
        }
        onAssetActivated: function(destination) {
            root.assetActivated(destination)
        }
    }
}
