pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PhotoEditor

AppDialog {
    id: root
    objectName: "sceneSaveDialog"
    title: "Save scene to Library"
    width: Math.min(420, popupAvailableWidth)
    anchors.centerIn: parent
    property string kind: "gaussian"
    property string projectPath: ""
    property string defaultName: "Untitled scene"
    property var sceneViewport: null
    property var beforeSave: null
    property string error: ""
    signal saved(string name)
    Theme { id: theme }
    SceneLibraryController { id: shelf }
    onOpened: {
        shelf.refresh()
        const existing = shelf.scenes.find(scene => scene.projectPath === root.projectPath)
        nameField.text = existing ? existing.name : root.defaultName
        root.error = ""
        nameField.forceActiveFocus(); nameField.selectAll()
    }
    contentItem: ColumnLayout {
        spacing: theme.space12
        ToneTextField {
            id: nameField
            objectName: "sceneLibraryNameField"
            Layout.fillWidth: true
            maximumLength: 120
            placeholderText: "Scene name"
            Accessible.name: "Scene name"
            onAccepted: if (saveButton.enabled) saveButton.clicked()
        }
        Label { visible: root.error.length > 0; Layout.fillWidth: true; text: root.error; wrapMode: Text.WordWrap; color: theme.accentWarm400 }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ActionButton { compact: true; text: "Cancel"; onClicked: root.close() }
            ActionButton {
                id: saveButton
                objectName: "sceneLibrarySaveConfirm"
                text: "Save scene"; compact: true; tone: "primary"
                enabled: nameField.text.trim().length > 0
                onClicked: {
                    const prepared = typeof root.beforeSave === "function" ? root.beforeSave(nameField.text.trim()) : {ok: true}
                    if (!prepared.ok) { root.error = prepared.error || "Could not save the scene view."; return }
                    const result = shelf.saveScene(root.kind, root.projectPath, nameField.text.trim(), String(prepared.id || ""), root.sceneViewport)
                    if (!result.ok) root.error = result.error
                    else { root.saved(result.name); root.close() }
                }
            }
        }
    }
}
