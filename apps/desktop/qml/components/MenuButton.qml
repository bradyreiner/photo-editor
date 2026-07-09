import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string text: ""
    property string iconName: ""
    property string tone: "secondary"
    property color accent: theme.accentWarm400
    property bool compact: false
    property bool active: false
    property bool enabled: true
    property bool allowLabelWrap: false
    property bool menuEnabled: (menuItems || []).length > 0
    property bool primaryOpensMenu: false
    property bool secondaryActionMenu: false
    property var menuItems: []
    property string tooltipText: ""
    property string menuTooltipText: text.length > 0 ? "More options for " + text : "More options"
    property int menuWidth: theme.menuMinWidth
    property int menuItemMaxLines: 2
    readonly property int menuMinimumWidth: theme.menuMinWidth
    readonly property int menuMaximumWidth: theme.menuMaxWidth
    readonly property int menuPopoverPadding: theme.space6
    signal clicked()
    signal menuItemTriggered(string key)

    Theme { id: theme }

    implicitHeight: compact ? theme.controlHeightDense : theme.controlHeight
    implicitWidth: primaryButton.implicitWidth + (root.menuEnabled ? chevronButton.implicitWidth + 1 : 0)
    opacity: root.enabled ? 1.0 : 0.56

    Behavior on opacity {
        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }
    }

    readonly property int checkedItemCount: countCheckedItems()
    readonly property int disabledItemCount: countDisabledItems()
    readonly property int disabledItemReasonCount: countDisabledItemsWithReasons()
    readonly property int dangerItemCount: countDangerItems()
    readonly property bool disabledItemsExplainState: disabledItemCount === disabledItemReasonCount
    readonly property bool destructiveItemsSeparated: dangerItemCount === 0 || firstDangerIndex() > 0
    readonly property bool destructiveItemsLast: dangerItemCount === 0 || nonDangerAfterFirstDangerIndex() < 0
    readonly property bool secondaryActionsUseMore: !secondaryActionMenu || text === "More"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|menu-wrap:true|menu-lines:" +
                                                   String(root.menuItemMaxLines) +
                                                   "|popover-width:min-content|tooltip-full:true"
    readonly property string menuGeometryContract: "menu-geometry:v1|min-width:" +
                                                   String(root.menuMinimumWidth) +
                                                   "|max-width:" +
                                                   String(root.menuMaximumWidth) +
                                                   "|padding:" +
                                                   String(root.menuPopoverPadding) +
                                                   "|item-height:32-36|icon-column:true|shortcut-column:true|labels-wrap:true"
    readonly property string secondaryActionSemanticsContract: "secondary-action-menu:v1|secondary:" +
                                                               String(root.secondaryActionMenu) +
                                                               "|label:" +
                                                               root.text +
                                                               "|uses-more:" +
                                                               String(root.secondaryActionsUseMore) +
                                                               "|tooltip-specific:" +
                                                               String(root.tooltipText.length > 0) +
                                                               "|destructive-deliberate:" +
                                                               String(root.destructiveItemsSeparated &&
                                                                      root.destructiveItemsLast)
    readonly property string disabledStateMotionContract: "disabled-state-motion:v1|duration:110|opacity:true|layout-stable:true|tooltip-preserved:true|interruptible:true"
    readonly property string menuContract: "menu-button:v1|items:" +
                                           String(root.menuItems ? root.menuItems.length : 0) +
                                           "|checked:" +
                                           String(checkedItemCount) +
                                           "|disabled:" +
                                           String(disabledItemCount) +
                                           "|danger:" +
                                           String(dangerItemCount) +
                                           "|danger-separated:" +
                                           String(destructiveItemsSeparated) +
                                           "|danger-last:" +
                                           String(destructiveItemsLast) +
                                           "|disabled-reasons-complete:" +
                                           String(disabledItemsExplainState) +
                                           "|keyboard:true|escape:true|disabled-reasons:true|checked-state:true|no-elide:true|readable-text:true|geometry:true|width:" +
                                           String(menuWidth)

    function countCheckedItems() {
        const items = root.menuItems || []
        let count = 0
        for (let index = 0; index < items.length; ++index) {
            if (items[index] && items[index].checked === true)
                ++count
        }
        return count
    }

    function countDisabledItems() {
        const items = root.menuItems || []
        let count = 0
        for (let index = 0; index < items.length; ++index) {
            if (items[index] && items[index].enabled === false)
                ++count
        }
        return count
    }

    function disabledReasonForItem(item) {
        if (!item)
            return ""
        const disabledReason = item.enabled === false
                ? String(item.disabledReason || item.reason || "Unavailable in the current state.")
                : String(item.disabledReason || item.reason || "")
        return disabledReason
    }

    function countDisabledItemsWithReasons() {
        const items = root.menuItems || []
        let count = 0
        for (let index = 0; index < items.length; ++index) {
            if (items[index] && items[index].enabled === false &&
                    disabledReasonForItem(items[index]).length > 0)
                ++count
        }
        return count
    }

    function countDangerItems() {
        const items = root.menuItems || []
        let count = 0
        for (let index = 0; index < items.length; ++index) {
            if (items[index] && (items[index].danger === true || items[index].destructive === true))
                ++count
        }
        return count
    }

    function firstDangerIndex() {
        const items = root.menuItems || []
        for (let index = 0; index < items.length; ++index) {
            if (items[index] && (items[index].danger === true || items[index].destructive === true))
                return index
        }
        return -1
    }

    function nonDangerAfterFirstDangerIndex() {
        const items = root.menuItems || []
        const firstDanger = root.firstDangerIndex()
        if (firstDanger < 0)
            return -1
        for (let index = firstDanger + 1; index < items.length; ++index) {
            if (items[index] && !(items[index].danger === true || items[index].destructive === true))
                return index
        }
        return -1
    }

    RowLayout {
        anchors.fill: parent
        spacing: 1

        ActionButton {
            id: primaryButton
            Layout.fillWidth: true
            Layout.preferredHeight: root.implicitHeight
            text: root.text
            iconName: root.iconName
            tone: root.tone
            accent: root.accent
            compact: root.compact
            allowLabelWrap: root.allowLabelWrap
            active: root.active
            enabled: root.enabled
            tooltipText: root.tooltipText
            onClicked: {
                if (root.primaryOpensMenu && root.menuEnabled)
                    menuPopup.open()
                else
                    root.clicked()
            }
        }

        ActionButton {
            id: chevronButton
            visible: root.menuEnabled
            Layout.preferredWidth: root.implicitHeight
            Layout.preferredHeight: root.implicitHeight
            showText: false
            iconName: "chevron-right"
            tone: root.tone === "primary" ? "secondary" : root.tone
            accent: root.accent
            compact: root.compact
            active: menuPopup.visible
            enabled: root.enabled && root.menuEnabled
            tooltipText: root.menuTooltipText
            onClicked: menuPopup.open()

            contentItem.rotation: 90
        }
    }

    Popup {
        id: menuPopup
        property int currentFocusIndex: -1

        function itemEnabledAt(index) {
            if (index < 0 || index >= root.menuItems.length)
                return false
            return root.menuItems[index].enabled !== false
        }

        function firstEnabledIndex() {
            for (let i = 0; i < root.menuItems.length; ++i) {
                if (itemEnabledAt(i))
                    return i
            }
            return -1
        }

        function checkedIndex() {
            for (let i = 0; i < root.menuItems.length; ++i) {
                if (itemEnabledAt(i) && root.menuItems[i].checked === true)
                    return i
            }
            return -1
        }

        function focusMenuItem(index) {
            if (!itemEnabledAt(index))
                return false
            const item = menuRepeater.itemAt(index)
            if (!item)
                return false
            currentFocusIndex = index
            item.forceActiveFocus(Qt.TabFocusReason)
            return true
        }

        function focusNextMenuItem(step) {
            if (root.menuItems.length === 0)
                return
            const start = currentFocusIndex >= 0 ? currentFocusIndex : firstEnabledIndex()
            for (let offset = 1; offset <= root.menuItems.length; ++offset) {
                const index = (start + step * offset + root.menuItems.length) % root.menuItems.length
                if (focusMenuItem(index))
                    return
            }
        }

        y: root.height + theme.space6
        width: Math.min(root.menuMaximumWidth,
                        Math.max(root.menuMinimumWidth, root.menuWidth, root.width,
                                 menuColumn.implicitWidth + root.menuPopoverPadding * 2))
        modal: false
        focus: true
        padding: root.menuPopoverPadding
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            currentFocusIndex = checkedIndex()
            if (currentFocusIndex < 0)
                currentFocusIndex = firstEnabledIndex()
            if (currentFocusIndex >= 0)
                Qt.callLater(function() { menuPopup.focusMenuItem(currentFocusIndex) })
        }
        contentItem: ColumnLayout {
            id: menuColumn
            spacing: theme.space4
            focus: true
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Down) {
                    menuPopup.focusNextMenuItem(1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Up) {
                    menuPopup.focusNextMenuItem(-1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Home) {
                    menuPopup.focusMenuItem(menuPopup.firstEnabledIndex())
                    event.accepted = true
                } else if (event.key === Qt.Key_End) {
                    for (let i = root.menuItems.length - 1; i >= 0; --i) {
                        if (menuPopup.focusMenuItem(i))
                            break
                    }
                    event.accepted = true
                }
            }

            Repeater {
                id: menuRepeater
                model: root.menuItems

                delegate: PopupMenuRow {
                    id: menuItem
                    previousItem: index > 0 ? root.menuItems[index - 1] : ({})
                    accent: root.accent
                    labelMaxLines: root.menuItemMaxLines
                    tooltipTimeout: 3000
                    onFocusIndexRequested: function(itemIndex) { menuPopup.currentFocusIndex = itemIndex }
                    onActivated: function(key) {
                        root.menuItemTriggered(key)
                        menuPopup.close()
                    }
                    onNavigateRequested: function(direction) { menuPopup.focusNextMenuItem(direction) }
                    onFirstRequested: menuPopup.focusMenuItem(menuPopup.firstEnabledIndex())
                    onLastRequested: {
                        for (let i = root.menuItems.length - 1; i >= 0; --i) {
                            if (menuPopup.focusMenuItem(i))
                                break
                        }
                    }
                }
            }
        }

        background: PopupSurface {
            accent: root.accent
        }
    }
}
