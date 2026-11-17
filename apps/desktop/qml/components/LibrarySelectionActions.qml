import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var catalogAssets
    property bool actionContextActive: true
    property bool deleteAttempted: false
    property var pendingAssetIds: []
    readonly property int selectedCount: Number(root.catalogAssets.selectedAssetCount || 0)
    readonly property bool hasContent: root.selectedCount > 0 || root.catalogAssets.deleting ||
        (root.deleteAttempted && root.catalogAssets.errorMessage.length > 0)
    readonly property bool canDelete: root.actionContextActive && root.selectedCount > 0 &&
        !root.catalogAssets.busy && !root.catalogAssets.deleting
    readonly property bool confirmationVisible: deleteDialog.visible
    readonly property bool contextMenuVisible: imageContextMenu.visible
    signal deleteRequested()
    spacing: theme.space6

    Theme { id: theme }

    function selectionMatchesConfirmation() {
        const selected = (root.catalogAssets.selectedAssetIds || []).slice(0).sort()
        return root.pendingAssetIds.length > 0 &&
            JSON.stringify(selected) === JSON.stringify(root.pendingAssetIds.slice(0).sort())
    }

    function requestDelete() {
        if (!root.canDelete)
            return false
        root.pendingAssetIds = (root.catalogAssets.selectedAssetIds || []).slice(0)
        if (root.pendingAssetIds.length !== root.selectedCount)
            return false
        deleteDialog.open()
        return true
    }

    function openImageMenu(index, anchor, x, y) {
        if (!root.actionContextActive || root.catalogAssets.busy || root.catalogAssets.deleting ||
                index < 0 || index >= root.catalogAssets.assetCount)
            return false
        const assetId = String(root.catalogAssets.assetAt(index).assetId || "")
        if (assetId.length === 0)
            return false
        // Preserve a selected batch when opening its context menu. Clicking a
        // different image targets that image alone, even in selection mode.
        if (root.catalogAssets.assetSelected(assetId))
            root.catalogAssets.selectIndex(index)
        else
            root.catalogAssets.selectIndexWithModifiers(index, 0, false)
        imageContextMenu.parent = anchor
        imageContextMenu.x = x
        imageContextMenu.y = y
        imageContextMenu.open()
        return true
    }

    onActionContextActiveChanged: {
        if (!root.actionContextActive) {
            deleteDialog.close()
            imageContextMenu.close()
        }
    }

    Connections {
        target: root.catalogAssets
        function onSelectionChanged() {
            if (deleteDialog.visible && !root.selectionMatchesConfirmation())
                deleteDialog.close()
            imageContextMenu.close()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: theme.space8
        Label {
            objectName: "librarySelectedImageCount"
            visible: root.selectedCount > 1 || root.catalogAssets.deleting
            text: root.catalogAssets.deleting
                  ? qsTr("Removing %1 of %2…").arg(root.catalogAssets.deleteProgress).arg(root.catalogAssets.deleteTargetCount)
                  : qsTr("%1 selected").arg(root.selectedCount)
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }
        ActionButton {
            objectName: "libraryDeleteSelectedButton"
            Layout.fillWidth: true
            text: root.selectedCount === 1 ? qsTr("Delete Image") : qsTr("Delete Selected")
            iconName: "delete"
            compact: true
            tone: "danger"
            enabled: root.canDelete
            tooltipText: qsTr("Remove the selected images from this Library. Source files stay on disk.")
            disabledReason: qsTr("Wait for the current catalog operation to finish.")
            onClicked: root.requestDelete()
        }
    }

    AppMenu {
        id: imageContextMenu
        objectName: "libraryImageContextMenu"
        AppMenuItem {
            objectName: "libraryContextDeleteItem"
            text: root.selectedCount === 1 ? qsTr("Delete Image…") : qsTr("Delete Selected…")
            iconName: "delete"
            danger: true
            enabled: root.canDelete
            onTriggered: {
                imageContextMenu.close()
                root.requestDelete()
            }
        }
    }

    Label {
        objectName: "libraryDeleteError"
        Layout.fillWidth: true
        visible: root.deleteAttempted && root.catalogAssets.errorMessage.length > 0
        text: root.catalogAssets.errorMessage
        color: theme.error
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.WordWrap
    }

    AppDialog {
        id: deleteDialog
        objectName: "libraryDeleteConfirmationDialog"
        parent: Overlay.overlay
        width: Math.min(popupAvailableWidth, theme.scaled(440))
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        title: root.pendingAssetIds.length === 1 ? qsTr("Delete selected image?")
                                               : qsTr("Delete %1 selected images?").arg(root.pendingAssetIds.length)
        onClosed: root.pendingAssetIds = []
        contentItem: Label {
            objectName: "libraryDeleteConsequences"
            text: qsTr("This removes the selected images and their saved edits from this Library. Source files stay on disk. You can re-import the files later, but their saved edits will be removed.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeBody
            wrapMode: Text.WordWrap
        }
        footer: DialogButtonBox {
            padding: theme.popupPadding
            topPadding: 0
            spacing: theme.space8
            background: Item {}
            ActionButton {
                objectName: "libraryDeleteCancelButton"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: deleteDialog.close()
            }
            ActionButton {
                objectName: "libraryDeleteConfirmButton"
                text: qsTr("Delete")
                tone: "danger"
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                enabled: root.canDelete && root.selectionMatchesConfirmation()
                onClicked: {
                    if (!root.canDelete || !root.selectionMatchesConfirmation())
                        return
                    root.deleteAttempted = true
                    root.catalogAssets.deleteSelectedAssets()
                    deleteDialog.close()
                    root.deleteRequested()
                }
            }
        }
    }
}
