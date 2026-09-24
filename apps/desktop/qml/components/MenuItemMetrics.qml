import QtQml

QtObject {
    id: root

    property var items: []

    readonly property int checkedItemCount: countCheckedItems()
    readonly property int disabledItemCount: countDisabledItems()
    readonly property int disabledItemReasonCount: countDisabledItemsWithReasons()
    readonly property int dangerItemCount: countDangerItems()
    readonly property bool disabledItemsExplainState: disabledItemCount === disabledItemReasonCount
    readonly property bool destructiveItemsSeparated: dangerItemCount === 0 || firstDangerIndex() > 0
    readonly property bool destructiveItemsLast: dangerItemCount === 0 || nonDangerAfterFirstDangerIndex() < 0

    function countCheckedItems() {
        let count = 0
        for (let index = 0; index < root.items.length; ++index) {
            if (root.items[index] && root.items[index].checked === true)
                ++count
        }
        return count
    }

    function countDisabledItems() {
        let count = 0
        for (let index = 0; index < root.items.length; ++index) {
            if (root.items[index] && root.items[index].enabled === false)
                ++count
        }
        return count
    }

    function disabledReasonForItem(item) {
        if (!item)
            return ""
        return item.enabled === false
                ? String(item.disabledReason || item.reason || "Unavailable in the current state.")
                : String(item.disabledReason || item.reason || "")
    }

    function countDisabledItemsWithReasons() {
        let count = 0
        for (let index = 0; index < root.items.length; ++index) {
            if (root.items[index] && root.items[index].enabled === false &&
                    disabledReasonForItem(root.items[index]).length > 0)
                ++count
        }
        return count
    }

    function countDangerItems() {
        let count = 0
        for (let index = 0; index < root.items.length; ++index) {
            if (root.items[index] &&
                    (root.items[index].danger === true || root.items[index].destructive === true))
                ++count
        }
        return count
    }

    function firstDangerIndex() {
        for (let index = 0; index < root.items.length; ++index) {
            if (root.items[index] &&
                    (root.items[index].danger === true || root.items[index].destructive === true))
                return index
        }
        return -1
    }

    function nonDangerAfterFirstDangerIndex() {
        const firstDanger = root.firstDangerIndex()
        if (firstDanger < 0)
            return -1
        for (let index = firstDanger + 1; index < root.items.length; ++index) {
            if (root.items[index] &&
                    !(root.items[index].danger === true || root.items[index].destructive === true))
                return index
        }
        return -1
    }
}
