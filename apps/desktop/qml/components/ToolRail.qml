import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Item {
    id: root

    property var tools: []
    property color accent: theme.accentWarm400
    property int buttonSize: theme.railButtonSizeCompact
    property int menuWidth: 220
    property int spacing: theme.space8
    property int menuItemMaxLines: 2
    property bool showLabels: false
    property bool compact: true
    property int switchSerial: 0
    readonly property int menuMinimumWidth: theme.menuMinWidth
    readonly property int menuMaximumWidth: theme.menuMaxWidth
    readonly property int menuPopoverPadding: theme.space6

    signal toolTriggered(var tool)
    signal menuItemTriggered(var tool, string key)

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

    onToolsChanged: closeOpenMenus()
    onSwitchSerialChanged: closeOpenMenus()

    function closeOpenMenus() {
        if (!toolRepeater)
            return
        for (let index = 0; index < toolRepeater.count; ++index) {
            const item = toolRepeater.itemAt(index)
            if (item && item.closeToolMenu)
                item.closeToolMenu()
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

    function toolDisabledReason(tool) {
        return String(tool && tool.disabledReason !== undefined ? tool.disabledReason : "")
    }

    function toolEnabled(tool) {
        return tool && tool.enabled !== false
    }

    function menuActions(tool) {
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
        const parts = [toolLabel(tool)]
        const variant = toolVariant(tool)
        const shortcut = toolShortcut(tool)
        if (variant.length > 0)
            parts.push(variant)
        if (shortcut.length > 0)
            parts.push(shortcut)
        return parts.join(" / ")
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
            model: root.tools

            delegate: Item {
                id: toolDelegate
                required property var modelData
                readonly property var tool: modelData || ({})
                readonly property var toolMenuItems: root.menuActions(tool)
                readonly property bool menuAvailable: toolMenuItems.length > 0
                readonly property string activeFamilyLabel: tool.active === true ? root.toolLabel(tool) : ""
                readonly property string activeVariantLabel: tool.active === true ? root.toolVariant(tool) : ""

                Layout.preferredWidth: root.showLabels ? Math.max(root.buttonSize, toolButton.implicitWidth) : root.buttonSize
                Layout.preferredHeight: root.buttonSize

                function closeToolMenu() {
                    railMenuPopup.close()
                }

                function openToolMenu() {
                    root.closeOpenMenus()
                    railMenuPopup.open()
                }

                Components.ToolButton {
                    id: toolButton
                    objectName: String(toolDelegate.tool.objectName || "")
                    anchors.fill: parent
                    label: root.toolLabel(toolDelegate.tool)
                    iconName: String(toolDelegate.tool.icon || "")
                    shortcut: root.toolShortcut(toolDelegate.tool)
                    variantLabel: root.toolVariant(toolDelegate.tool)
                    statusText: String(toolDelegate.tool.statusText || "")
                    disabledReason: root.toolDisabledReason(toolDelegate.tool)
                    accent: root.accent
                    active: toolDelegate.tool.active === true
                    pending: String(toolDelegate.tool.pendingState || "") === "pending" ||
                             String(toolDelegate.tool.pendingState || "") === "active"
                    warning: String(toolDelegate.tool.pendingState || "") === "failed"
                    commandEnabled: root.toolEnabled(toolDelegate.tool)
                    compact: root.compact
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

                Popup {
                    id: railMenuPopup
                    property int currentFocusIndex: -1

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

                    x: toolDelegate.width + theme.space6
                    y: 0
                    width: Math.min(root.menuMaximumWidth,
                                    Math.max(root.menuMinimumWidth,
                                             root.menuWidthForTool(toolDelegate.tool),
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

                    background: PopupSurface {
                        accent: root.accent
                    }
                }
            }
        }
    }
}
