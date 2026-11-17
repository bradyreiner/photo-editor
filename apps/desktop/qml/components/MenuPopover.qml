import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root

    property var menuItems: []
    property color accent: theme.accentWarm400
    property int anchorHeight: 0
    property int anchorWidth: 0
    property int menuWidth: theme.menuMinWidth
    property int menuItemMaxLines: 2
    property int menuMinimumWidth: theme.menuMinWidth
    property int menuMaximumWidth: theme.menuMaxWidth
    property int menuPopoverPadding: theme.space6
    property string ownerObjectName: ""
    property bool strongSurface: false
    property int currentFocusIndex: -1
    property bool activationPending: false
    transientMenu: true

    signal itemTriggered(string key)

    Theme {
        id: theme
    }

    objectName: ownerObjectName.length > 0 ? ownerObjectName + "Popup" : ""

    function itemEnabledAt(index) {
        if (index < 0 || index >= root.menuItems.length)
            return false;
        return root.menuItems[index].enabled !== false;
    }

    function firstEnabledIndex() {
        for (let index = 0; index < root.menuItems.length; ++index) {
            if (itemEnabledAt(index))
                return index;
        }
        return -1;
    }

    function activateItem(key) {
        if (root.activationPending || !root.visible)
            return
        root.activationPending = true
        // Closing or executing an action can synchronously replace the row
        // model. Let its clicked/activated/Keys handlers finish first.
        Qt.callLater(root.finishItemActivation, String(key || ""))
    }

    function finishItemActivation(action) {
        if (!root.activationPending)
            return
        root.activationPending = false
        if (!root.visible)
            return
        root.closeTransient(false)
        root.itemTriggered(action)
    }

    Connections {
        target: root
        function onAboutToShow() { root.activationPending = false }
        function onAboutToHide() { root.activationPending = false }
    }

    function checkedIndex() {
        for (let index = 0; index < root.menuItems.length; ++index) {
            if (itemEnabledAt(index) && root.menuItems[index].checked === true)
                return index;
        }
        return -1;
    }

    function focusMenuItem(index) {
        return root.focusMenuItemWithReason(index, Qt.TabFocusReason);
    }

    function focusMenuItemWithReason(index, reason) {
        if (!itemEnabledAt(index))
            return false;
        const item = menuRepeater.itemAt(index);
        if (!item)
            return false;
        currentFocusIndex = index;
        item.forceActiveFocus(reason === undefined ? Qt.TabFocusReason : reason);
        if (item.y < menuScroll.contentY)
            menuScroll.contentY = item.y;
        else if (item.y + item.height > menuScroll.contentY + menuScroll.height)
            menuScroll.contentY = Math.max(0, item.y + item.height - menuScroll.height);
        return true;
    }

    function focusNextMenuItem(step) {
        if (root.menuItems.length === 0)
            return;
        const start = currentFocusIndex >= 0 ? currentFocusIndex : firstEnabledIndex();
        for (let offset = 1; offset <= root.menuItems.length; ++offset) {
            const index = (start + step * offset + root.menuItems.length) % root.menuItems.length;
            if (focusMenuItem(index))
                return;
        }
    }

    function toggleFrom(opener) {
        return root.toggleTransient(opener)
    }

    y: root.anchorHeight + theme.space6
    margins: theme.space8
    width: Math.min(popupAvailableWidth, Math.max(1, Math.min(root.Overlay.overlay ? Math.max(1, root.Overlay.overlay.width - margins * 2) : root.menuMaximumWidth, root.menuMaximumWidth, Math.max(root.menuMinimumWidth, root.menuWidth, root.anchorWidth, menuColumn.implicitWidth + root.menuPopoverPadding * 2))))
    height: Math.min(popupAvailableHeight, Math.max(1, Math.min(menuColumn.implicitHeight + padding * 2, root.Overlay.overlay ? Math.max(1, root.Overlay.overlay.height - margins * 2) : menuColumn.implicitHeight + padding * 2)))
    modal: false
    focus: true
    padding: root.menuPopoverPadding
    closePolicy: Popup.CloseOnPressOutside
    onOpened: {
        currentFocusIndex = checkedIndex();
        if (currentFocusIndex < 0)
            currentFocusIndex = firstEnabledIndex();
        if (currentFocusIndex >= 0)
            Qt.callLater(function () {
                // Pointer-opened menus keep keyboard routing without drawing a
                // keyboard-focus box around the first row before navigation.
                root.focusMenuItemWithReason(currentFocusIndex,
                    root.transientOpener && !root.transientOpener.visualFocus
                        ? Qt.MouseFocusReason : Qt.TabFocusReason);
            });
    }

    contentItem: Flickable {
        id: menuScroll
        objectName: root.ownerObjectName.length > 0 ? root.ownerObjectName + "Scroll" : "menuPopoverScroll"
        clip: true
        contentWidth: width
        contentHeight: menuColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        ColumnLayout {
            id: menuColumn
            readonly property bool transientPopupKeyboardScope: true
            width: menuScroll.width
            spacing: theme.space2
            focus: true

            Keys.onPressed: function (event) {
                if (event.key === Qt.Key_Escape) {
                    root.closeTransient(true);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Down) {
                    root.focusNextMenuItem(1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Up) {
                    root.focusNextMenuItem(-1);
                    event.accepted = true;
                } else if (event.key === Qt.Key_Home) {
                    root.focusMenuItem(root.firstEnabledIndex());
                    event.accepted = true;
                } else if (event.key === Qt.Key_End) {
                    for (let index = root.menuItems.length - 1; index >= 0; --index) {
                        if (root.focusMenuItem(index))
                            break;
                    }
                    event.accepted = true;
                }
            }

            Repeater {
                id: menuRepeater
                model: root.visible ? root.menuItems : []

                delegate: PopupMenuRow {
                    onDismissRequested: Qt.callLater(root.closeTransient, true)
                    previousItem: index > 0 ? root.menuItems[index - 1] : ({})
                    objectNamePrefix: root.ownerObjectName.length > 0
                                      ? root.ownerObjectName + "Popup"
                                      : ""
                    accent: root.accent
                    labelMaxLines: root.menuItemMaxLines
                    tooltipTimeout: 3000
                    onFocusIndexRequested: function (itemIndex) {
                        root.currentFocusIndex = itemIndex;
                    }
                    onActivated: function (key) {
                        root.activateItem(key);
                    }
                    onNavigateRequested: function (direction) {
                        root.focusNextMenuItem(direction);
                    }
                    onFirstRequested: root.focusMenuItem(root.firstEnabledIndex())
                    onLastRequested: {
                        for (let itemIndex = root.menuItems.length - 1; itemIndex >= 0; --itemIndex) {
                            if (root.focusMenuItem(itemIndex))
                                break;
                        }
                    }
                }
            }
        }
    }

    background: PopupSurface {}
}
