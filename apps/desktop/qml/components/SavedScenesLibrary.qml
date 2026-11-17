pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PhotoEditor

Item {
    id: root
    objectName: "savedScenesLibrary"
    signal openRequested(var scene)
    signal createRequested()
    Theme { id: theme }
    SceneLibraryController { id: shelf }
    onVisibleChanged: if (visible) shelf.refresh()
    ColumnLayout {
        anchors.fill: parent
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Label { text: "Scenes"; color: theme.textStrong; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
                Label { text: shelf.scenes.length + " saved · opens in Scene"; color: theme.textMuted; font.pixelSize: theme.typeCaption }
            }
            ActionButton { compact: true; text: "Refresh"; iconName: "refresh"; onClicked: shelf.refresh() }
            ActionButton { objectName: "librarySceneWorkstationButton"; compact: true; text: "Open workstation"; onClicked: root.createRequested() }
        }
        Label { Layout.fillWidth: true; visible: shelf.error.length > 0; text: shelf.error; color: theme.accentWarm400; wrapMode: Text.WordWrap }
        GridView {
            id: grid
            objectName: "savedScenesGrid"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; model: shelf.scenes
            cellWidth: width / Math.max(1, Math.floor(width / 300)); cellHeight: theme.scaled(270)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                width: grid.cellWidth - 14; height: grid.cellHeight - 14
                color: theme.surfaceInset; radius: theme.radiusSurface
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: theme.space12; spacing: theme.space8
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 100
                        color: theme.mediaCanvas
                        Image { id: thumbnail; anchors.fill: parent; source: String(modelData.previewSource || ""); asynchronous: true; fillMode: Image.PreserveAspectFit }
                        AppIcon { anchors.centerIn: parent; width: 32; height: 32; name: modelData.kind === "depth-motion" ? "layers" : "geometry"; visible: thumbnail.source.toString().length === 0; color: theme.textMuted }
                    }
                    Label { Layout.fillWidth: true; text: modelData.name; color: theme.textStrong; font.weight: Font.DemiBold; elide: Text.ElideRight }
                    Label { text: modelData.available ? (modelData.kind === "depth-motion" ? "3D Motion · video point cloud" : "3D scene") : "Project unavailable"; color: theme.textMuted; font.pixelSize: theme.typeCaption }
                    ActionButton {
                        objectName: "libraryOpenSceneButton" + modelData.id
                        Layout.fillWidth: true; compact: true; text: "Open in Scene"; enabled: modelData.available
                        onClicked: root.openRequested(modelData)
                    }
                }
            }
            Label { anchors.centerIn: parent; visible: shelf.scenes.length === 0; text: "Save a scene from the Scene workstation.\nYour scenes and point-cloud videos appear here."; color: theme.textMuted; horizontalAlignment: Text.AlignHCenter; lineHeight: 1.5 }
        }
    }
}
