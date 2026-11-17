pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Compatibility host. SceneWorkspace presents the workspace directly.
AppDialog {
    id: root
    objectName: "depthMotionDialog"
    title: "3D Motion"
    width: Math.min(1180, popupAvailableWidth)
    height: Math.min(900, popupAvailableHeight)
    anchors.centerIn: parent
    property alias sourcePath: workspace.sourcePath
    property alias depthPath: workspace.depthPath
    property alias settingsChanged: workspace.settingsChanged
    contentItem: DepthMotionWorkspace { id: workspace; onExitRequested: root.close() }
    onClosed: { workspace.pointCloudViewport.playing = false; workspace.saveView() }
}
