pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Item {
    id: root

    property var tools: []
    property var toolIdentityModel: []
    property color accent: theme.accentWarm400
    property int buttonSize: theme.railButtonSizeCompact
    property int menuWidth: 220
    property int spacing: theme.space8
    property int menuItemMaxLines: 2
    property bool showLabels: false
    property bool compact: true
    property bool menusEnabled: true
    property int toolIconSize: 0
    property int switchSerial: 0
    readonly property int menuMinimumWidth: theme.menuMinWidth
    readonly property int menuMaximumWidth: theme.menuMaxWidth
    readonly property int menuPopoverPadding: theme.space6

    signal toolTriggered(var tool)
    signal menuItemTriggered(var tool, string key)
    signal closeMenusRequested()

    Theme { id: theme }

    implicitWidth: root.buttonSize
    implicitHeight: toolColumn.implicitHeight
    readonly property int menuToolCount: countMenuTools()
    readonly property int lifecycleToolCount: countLifecycleTools()
    readonly property string activeVariantLabel: currentActiveVariantLabel()
    readonly property bool activeVariantVisible: activeVariantLabel.length > 0
    readonly property string expansionContract: "tool-rail:v1|tools:" +
                                                String((root.tools || []).length) +
                                                "|menus:" +
                                                String(menuToolCount) +
                                                "|activeVariant:" +
                                                activeVariantLabel +
                                                "|variantVisible:" +
                                                String(activeVariantVisible) +
                                                "|lifecycle:" +
                                                String(lifecycleToolCount) +
                                                "|keyboard-menu:true|disabled-reasons:true|no-elide:true|readable-text:true|geometry:true"
    readonly property string lifecycleContract: "tool-rail-lifecycle:v1|declared:" +
                                                String(lifecycleToolCount) +
                                                "|tools:" +
                                                String((root.tools || []).length) +
                                                "|status-tooltips:true|shared-switching-model:true"
    readonly property string toolDiscoveryContract: "tool-discovery:v1|families-visible:true|active-family:true|active-variant:true|variants-one-expansion:true|stale-menus-close:true|disabled-reasons:true"
    readonly property string staleMenuCleanupContract: "tool-rail-stale-menu-cleanup:v1|one-expansion:true|on-tool-click:true|on-menu-open:true|on-menu-trigger:true|on-tools-changed:true|on-switch-serial:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|rail-labels:fit|menu-wrap:true|tooltip-full:true|variant-visible:true"
    readonly property string groupedNavigationContract: "tool-rail-navigation:v2|groups:" +
                                                        railGroupOrder().join(">") +
                                                        "|button-size:" +
                                                        String(root.buttonSize) +
                                                        "|stable-size:true|hover-layout-shift:false|tooltips:explicit|variants:" +
                                                        (root.menusEnabled ? "menus" : "page-controls") +
                                                        "|one-expansion:" + String(root.menusEnabled)
    readonly property string directNavigationContract: "tool-rail-direct-navigation:v1|enabled:" +
                                                       String(!root.menusEnabled) +
                                                       "|click:page|menus:" + String(root.menusEnabled) +
                                                       "|hover-overlays:" + String(root.menusEnabled) +
                                                       "|variants:" + (root.menusEnabled ? "rail" : "page")
    readonly property string activationCoverageContract: "tool-rail-activation:v1|primary-tools:" +
                                                         primaryToolIds().join(",") +
                                                         "|source:rail-model|buttons:present|trigger-by-id:true"

    onToolsChanged: {
        closeOpenMenus()
        syncToolIdentityModel()
    }
    onSwitchSerialChanged: closeOpenMenus()

    function closeOpenMenus() {
        root.closeMenusRequested()
    }

    function syncToolIdentityModel() {
        const list = root.tools || []
        const next = []
        for (let index = 0; index < list.length; ++index) {
            const id = toolId(list[index])
            next.push(id.length > 0 ? "id:" + id : "slot:" + index)
        }
        const current = root.toolIdentityModel || []
        if (current.length !== next.length)
            root.toolIdentityModel = next
        else {
            for (let index = 0; index < next.length; ++index) {
                if (String(current[index]) !== next[index]) {
                    root.toolIdentityModel = next
                    break
                }
            }
        }
    }

    function toolId(tool) {
        return String(tool && tool.id !== undefined ? tool.id : "")
    }

    function toolLabel(tool) {
        return String(tool && tool.label !== undefined ? tool.label : toolId(tool))
    }

    function toolVariant(tool) {
        return String(tool && tool.variant !== undefined ? tool.variant : "")
    }

    function toolShortcut(tool) {
        return String(tool && tool.shortcut !== undefined ? tool.shortcut : "")
    }

    function toolRailGroup(tool) {
        return String(tool && tool.railGroup !== undefined ? tool.railGroup : "")
    }

    function toolDisabledReason(tool) {
        return String(tool && tool.disabledReason !== undefined ? tool.disabledReason : "")
    }

    function toolEnabled(tool) {
        return tool && tool.enabled !== false
    }

    function menuActions(tool) {
        if (!root.menusEnabled)
            return []
        if (!toolEnabled(tool)) {
            const reason = toolDisabledReason(tool)
            return reason.length > 0 ? [{
                "key": "disabled",
                "label": reason,
                "icon": "info",
                "enabled": false,
                "disabledReason": reason
            }] : []
        }
        const actions = tool && tool.menuActions !== undefined ? tool.menuActions : []
        return actions || []
    }

    function toolTooltip(tool) {
        const reason = toolDisabledReason(tool)
        if (!toolEnabled(tool) && reason.length > 0)
            return toolLabel(tool) + " - " + reason
        const explicitTooltip = String(tool && tool.tooltip !== undefined ? tool.tooltip : "")
        if (explicitTooltip.length > 0)
            return explicitTooltip
        const parts = [toolLabel(tool)]
        const variant = toolVariant(tool)
        const shortcut = toolShortcut(tool)
        if (variant.length > 0)
            parts.push(variant)
        if (shortcut.length > 0)
            parts.push(shortcut)
        return parts.join(" / ")
    }

    function railGroupOrder() {
        const list = root.tools || []
        const groups = []
        for (let index = 0; index < list.length; ++index) {
            const group = toolRailGroup(list[index])
            if (group.length > 0 && groups.indexOf(group) < 0)
                groups.push(group)
        }
        return groups
    }

    function primaryToolIds() {
        const list = root.tools || []
        const ids = []
        for (let index = 0; index < list.length; ++index) {
            const tool = list[index]
            if (tool && tool.primaryTool !== false)
                ids.push(toolId(tool))
        }
        return ids
    }

    function triggerToolById(requestedToolId) {
        const requested = String(requestedToolId || "")
        const list = root.tools || []
        for (let index = 0; index < list.length; ++index) {
            const tool = list[index]
            if (toolId(tool) === requested) {
                if (!toolEnabled(tool))
                    return false
                root.closeOpenMenus()
                root.toolTriggered(tool)
                return true
            }
        }
        return false
    }

    function menuWidthForTool(tool) {
        const value = Number(tool && tool.menuWidth !== undefined ? tool.menuWidth : root.menuWidth)
        return Number.isFinite(value) && value > 0 ? value : root.menuWidth
    }

    function countMenuTools() {
        const list = root.tools || []
        let count = 0
        for (let index = 0; index < list.length; ++index) {
            if (menuActions(list[index]).length > 0)
                ++count
        }
        return count
    }

    function countLifecycleTools() {
        const list = root.tools || []
        let count = 0
        for (let index = 0; index < list.length; ++index) {
            const tool = list[index]
            const lifecycle = tool && tool.lifecycle !== undefined ? tool.lifecycle : ({})
            if (String(lifecycle.entry || "").length > 0 &&
                    String(lifecycle.primaryAction || "").length > 0 &&
                    String(lifecycle.commit || "").length > 0 &&
                    String(lifecycle.cancel || "").length > 0)
                ++count
        }
        return count
    }

    function currentActiveVariantLabel() {
        if (!root.menusEnabled)
            return ""
        const list = root.tools || []
        for (let index = 0; index < list.length; ++index) {
            const tool = list[index]
            if (tool && tool.active === true) {
                const variant = toolVariant(tool)
                if (variant.length > 0)
                    return variant
            }
        }
        return ""
    }

    ColumnLayout {
        id: toolColumn
        anchors.fill: parent
        spacing: root.spacing

        Repeater {
            id: toolRepeater
            model: root.toolIdentityModel

            delegate: Item {
                id: toolDelegate
                required property int index
                readonly property var tool: (root.tools || [])[index] || ({})
                readonly property var toolMenuItems: root.menuActions(tool)
                readonly property bool menuAvailable: toolMenuItems.length > 0
                readonly property string activeFamilyLabel: tool.active === true ? root.toolLabel(tool) : ""
                readonly property string activeVariantLabel: tool.active === true ? root.toolVariant(tool) : ""
                property real menuPointerX: root.buttonSize / 2
                property real menuPointerY: root.buttonSize / 2

                Layout.preferredWidth: root.showLabels ? Math.max(root.buttonSize, toolButton.implicitWidth) : root.buttonSize
                Layout.preferredHeight: root.buttonSize

                function closeToolMenu() {
                    railMenuPopup.close()
                }

                function openToolMenu() {
                    root.closeOpenMenus()
                    railMenuPopup.open()
                }

                function menuPopupX() {
                    const overlay = railMenuPopup.parent
                    if (!overlay)
                        return toolDelegate.x + toolDelegate.width + theme.space6
                    const point = toolDelegate.mapToItem(overlay, menuPointerX, menuPointerY)
                    return Math.max(theme.space8,
                                    Math.min(overlay.width - railMenuPopup.width - theme.space8,
                                             point.x + theme.space12))
                }

                function menuPopupY() {
                    const overlay = railMenuPopup.parent
                    if (!overlay)
                        return toolDelegate.y
                    const point = toolDelegate.mapToItem(overlay, menuPointerX, menuPointerY)
                    return Math.max(theme.space8,
                                    Math.min(overlay.height - railMenuPopup.height - theme.space8,
                                             point.y - railMenuPopup.height / 2))
                }

                Connections {
                    target: root
                    function onCloseMenusRequested() {
                        toolDelegate.closeToolMenu()
                    }
                }

                Components.ToolButton {
                    id: toolButton
                    objectName: String(toolDelegate.tool.railObjectName || toolDelegate.tool.objectName || "")
                    anchors.fill: parent
                    label: root.toolLabel(toolDelegate.tool)
                    iconName: String(toolDelegate.tool.icon || "")
                    shortcut: root.toolShortcut(toolDelegate.tool)
                    variantLabel: root.menusEnabled ? root.toolVariant(toolDelegate.tool) : ""
                    statusText: String(toolDelegate.tool.statusText || "")
                    disabledReason: root.toolDisabledReason(toolDelegate.tool)
                    accent: root.accent
                    active: toolDelegate.tool.active === true
                    pending: String(toolDelegate.tool.pendingState || "") === "pending" ||
                             String(toolDelegate.tool.pendingState || "") === "active"
                    warning: String(toolDelegate.tool.pendingState || "") === "failed"
                    commandEnabled: root.toolEnabled(toolDelegate.tool)
                    compact: root.compact
                    toolIconSize: root.toolIconSize > 0
                                  ? root.toolIconSize
                                  : (root.compact ? 14 : 16)
                    showLabel: root.showLabels
                    showMenuIndicator: toolDelegate.menuAvailable
                    onClicked: {
                        if (commandEnabled) {
                            root.closeOpenMenus()
                            root.toolTriggered(toolDelegate.tool)
                        } else if (toolDelegate.menuAvailable) {
                            toolDelegate.openToolMenu()
                        }
                    }
                    onMenuRequested: if (toolDelegate.menuAvailable) toolDelegate.openToolMenu()
                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Down || event.key === Qt.Key_Space) &&
                                ((event.modifiers & Qt.AltModifier) !== 0) &&
                                toolDelegate.menuAvailable) {
                            toolDelegate.openToolMenu()
                            event.accepted = true
                        }
                    }
                }

                HoverHandler {
                    enabled: toolDelegate.menuAvailable
                    onPointChanged: {
                        toolDelegate.menuPointerX = point.position.x
                        toolDelegate.menuPointerY = point.position.y
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    enabled: toolDelegate.menuAvailable
                    onTapped: toolDelegate.openToolMenu()
                }

                MouseArea {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: Math.min(parent.width, 18)
                    height: Math.min(parent.height, 18)
                    acceptedButtons: Qt.LeftButton
                    enabled: toolDelegate.menuAvailable
                    cursorShape: Qt.PointingHandCursor
                    onClicked: function(mouse) {
                        toolDelegate.openToolMenu()
                        mouse.accepted = true
                    }
                }

                AppPopup {
                    id: railMenuPopup
                    property int currentFocusIndex: -1
                    parent: Overlay.overlay

                    function itemEnabledAt(index) {
                        if (index < 0 || index >= toolDelegate.toolMenuItems.length)
                            return false
                        return toolDelegate.toolMenuItems[index].enabled !== false
                    }

                    function firstEnabledIndex() {
                        for (let i = 0; i < toolDelegate.toolMenuItems.length; ++i) {
                            if (itemEnabledAt(i))
                                return i
                        }
                        return -1
                    }

                    function checkedIndex() {
                        for (let i = 0; i < toolDelegate.toolMenuItems.length; ++i) {
                            if (itemEnabledAt(i) && toolDelegate.toolMenuItems[i].checked === true)
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
                        if (toolDelegate.toolMenuItems.length === 0)
                            return
                        const start = currentFocusIndex >= 0 ? currentFocusIndex : firstEnabledIndex()
                        for (let offset = 1; offset <= toolDelegate.toolMenuItems.length; ++offset) {
                            const index = (start + step * offset + toolDelegate.toolMenuItems.length) % toolDelegate.toolMenuItems.length
                            if (focusMenuItem(index))
                                return
                        }
                    }

                    x: toolDelegate.menuPopupX()
                    y: toolDelegate.menuPopupY()
                    width: Math.min(popupAvailableWidth, Math.max(1, Math.min(root.menuMaximumWidth,
                                    Math.max(root.menuMinimumWidth,
                                             root.menuWidthForTool(toolDelegate.tool),
                                             menuColumn.implicitWidth + root.menuPopoverPadding * 2))))
                    modal: false
                    focus: true
                    padding: root.menuPopoverPadding
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    onOpened: {
                        currentFocusIndex = checkedIndex()
                        if (currentFocusIndex < 0)
                            currentFocusIndex = firstEnabledIndex()
                        if (currentFocusIndex >= 0)
                            Qt.callLater(function() { railMenuPopup.focusMenuItem(currentFocusIndex) })
                    }

                    contentItem: ColumnLayout {
                        id: menuColumn
                        spacing: theme.space4
                        focus: true
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Down) {
                                railMenuPopup.focusNextMenuItem(1)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Up) {
                                railMenuPopup.focusNextMenuItem(-1)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Home) {
                                railMenuPopup.focusMenuItem(railMenuPopup.firstEnabledIndex())
                                event.accepted = true
                            } else if (event.key === Qt.Key_End) {
                                for (let i = toolDelegate.toolMenuItems.length - 1; i >= 0; --i) {
                                    if (railMenuPopup.focusMenuItem(i))
                                        break
                                }
                                event.accepted = true
                            }
                        }

                        Repeater {
                            id: menuRepeater
                            model: toolDelegate.toolMenuItems

                            delegate: PopupMenuRow {
                                id: menuItem
                                previousItem: index > 0 ? toolDelegate.toolMenuItems[index - 1] : ({})
                                accent: root.accent
                                labelMaxLines: root.menuItemMaxLines
                                tooltipTimeout: 3000
                                onFocusIndexRequested: function(itemIndex) { railMenuPopup.currentFocusIndex = itemIndex }
                                onActivated: function(key) {
                                    railMenuPopup.close()
                                    root.closeOpenMenus()
                                    root.menuItemTriggered(toolDelegate.tool, key)
                                }
                                onNavigateRequested: function(direction) { railMenuPopup.focusNextMenuItem(direction) }
                                onFirstRequested: railMenuPopup.focusMenuItem(railMenuPopup.firstEnabledIndex())
                                onLastRequested: {
                                    for (let i = toolDelegate.toolMenuItems.length - 1; i >= 0; --i) {
                                        if (railMenuPopup.focusMenuItem(i))
                                            break
                                    }
                                }
                            }
                        }
                    }

                    background: PopupSurface {}
                }
            }
        }
    }
}
