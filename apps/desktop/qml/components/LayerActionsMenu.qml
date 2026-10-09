pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: root
    Theme { id: theme }
    property var menuItems: []
    signal menuItemTriggered(string key)
    width: theme.scaled(264)

    function matching(keys, prefix) {
        return menuItems.filter(function(item) {
            return item.visible !== false && (keys.indexOf(item.key) >= 0 ||
                (prefix && String(item.key).indexOf(prefix) === 0))
        })
    }
    readonly property var primaryItems: matching([
        "properties", "copy-layers", "paste-layers", "paste-layers-in-place",
        "duplicate-with-masks", "relink-source", "toggle-lock"
    ])
    readonly property var groupedItems: [
        { key: "arrange", label: qsTr("Arrange"), items: matching(["transform", "move-up", "move-down", "move-out-of-group"], "move-into-group:") },
        { key: "selections", label: qsTr("Selections and masks"), items: matching(["toggle-mask", "toggle-clip", "erase-selection-pixels", "copy-selection-pixels", "copy-selection-pixels-independent", "use-selection-mask", "erase-background", "duplicate-without-masks"]) },
        { key: "visibility", label: qsTr("Visibility"), items: matching(["toggle-visible", "toggle-solo"]) },
        { key: "adjustments", label: qsTr("Adjustments"), items: matching(["copy-adjustments", "paste-adjustments", "paste-base-adjustments", "preview-adjustments", "sync-adjustments"]) }
    ].filter(function(group) { return group.items.length > 0 })
    readonly property var deleteItem: matching(["delete"])[0] || ({ enabled: false })

    function dispatch(key) {
        root.dismiss()
        root.menuItemTriggered(key)
    }

    Component {
        id: actionDelegate
        MenuItem {
            required property var modelData
            objectName: "layerAction_" + String(modelData.key || "")
            text: modelData.key === "duplicate-with-masks" ? qsTr("Duplicate Layer") : String(modelData.label || "")
            enabled: modelData.enabled !== false
            checkable: modelData.checked !== undefined
            checked: modelData.checked === true
            font.family: theme.fontUi
            font.pixelSize: theme.typeControl
            ToolTip.visible: hovered
            ToolTip.delay: 650
            ToolTip.text: enabled ? String(modelData.tooltip || text)
                                 : String(modelData.disabledReason || qsTr("Unavailable in the current state."))
            Accessible.description: ToolTip.text
            onTriggered: root.dispatch(String(modelData.key || ""))
        }
    }
    Instantiator {
        model: root.primaryItems
        delegate: actionDelegate
        onObjectAdded: function(index, object) { root.insertItem(index, object) }
        onObjectRemoved: function(index, object) { root.removeItem(object) }
    }
    Instantiator {
        model: root.groupedItems
        delegate: Menu {
            id: groupMenu
            required property var modelData
            objectName: "layerActions_" + modelData.key
            title: modelData.label
            width: root.width
            Instantiator {
                model: groupMenu.modelData.items
                delegate: actionDelegate
                onObjectAdded: function(index, object) { groupMenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { groupMenu.removeItem(object) }
            }
        }
        onObjectAdded: function(index, object) { root.insertMenu(root.primaryItems.length + index, object) }
        onObjectRemoved: function(index, object) { root.removeMenu(object) }
    }
    MenuSeparator { }
    MenuItem {
        objectName: "layerAction_delete"
        text: qsTr("Delete Layer")
        enabled: root.deleteItem.enabled !== false
        font.family: theme.fontUi
        font.pixelSize: theme.typeControl
        ToolTip.visible: hovered && !enabled
        ToolTip.text: String(root.deleteItem.disabledReason || "")
        onTriggered: root.dispatch("delete")
    }
}
