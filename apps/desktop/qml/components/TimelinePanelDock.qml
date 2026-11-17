pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

GlassSurface {
    id: root

    required property var theme
    required property var dockState
    required property string dockId
    property string title: ""
    // A floating window can bind one panel ID here so independent windows do
    // not share a single tab strip or active-panel selection.
    property string floatingPanelId: ""
    property var mergedDockIds: []
    property string preferredPanelId: ""
    property alias contentHost: panelContentHost
    default property alias contentData: panelContentHost.data
    readonly property int headerHeight: headerHost.y + headerHost.height
    readonly property string activePanelId: {
        dockState.activePanelByDock
        if (dockId === "floating" && floatingPanelId.length > 0)
            return dockState.visible(floatingPanelId) && dockState.dock(floatingPanelId) === "floating"
                    ? floatingPanelId : ""
        if (mergedDockIds.length > 0) {
            if (visiblePanelIds.indexOf(preferredPanelId) >= 0)
                return preferredPanelId
            const primary = dockState.activePanel(dockId)
            if (visiblePanelIds.indexOf(primary) >= 0)
                return primary
            for (let index = 0; index < mergedDockIds.length; ++index) {
                const candidate = dockState.activePanel(mergedDockIds[index])
                if (visiblePanelIds.indexOf(candidate) >= 0)
                    return candidate
            }
            return visiblePanelIds.length > 0 ? visiblePanelIds[0] : ""
        }
        return dockState.activePanel(dockId)
    }
    readonly property var visiblePanelIds: {
        dockState.panelState
        if (dockId === "floating" && floatingPanelId.length > 0)
            return dockState.visible(floatingPanelId) && dockState.dock(floatingPanelId) === "floating"
                    ? [floatingPanelId] : []
        let panels = dockState.panelsInDock(dockId, true)
        for (let index = 0; index < mergedDockIds.length; ++index) {
            const additional = dockState.panelsInDock(mergedDockIds[index], true)
            for (let panelIndex = 0; panelIndex < additional.length; ++panelIndex) {
                if (panels.indexOf(additional[panelIndex]) < 0)
                    panels.push(additional[panelIndex])
            }
        }
        return panels
    }
    readonly property string timelinePanelDockContract:
        "timeline-panel-dock:v2|tabs:independent-visible-state|actions:close-move-float-dock|content:owner-supplied-host|floating:per-window-panel-filter|style:glass-action-button-theme"

    signal panelClosed(string panelId)
    signal panelFocused(string panelId)
    signal panelMoveRequested(string panelId, string dockId)
    signal panelFloatRequested(string panelId, bool floating)

    radius: 0
    edgeOpacity: 0
    highlightOpacity: 0
    implicitHeight: headerHeight + theme.scaled(240)
    Accessible.role: Accessible.Pane
    Accessible.name: title.length > 0 ? title : dockId + " timeline dock"
    Keys.onEscapePressed: function(event) {
        if (root.dockState.draggedPanelId.length > 0) {
            root.dockState.cancelPanelDrag()
            event.accepted = true
        }
    }

    Component.onCompleted: dockState.registerDockHost(root)
    Component.onDestruction: if (dockState) dockState.unregisterDockHost(root)

    function dropIndexAt(globalX, globalY) {
        const point = tabRow.mapFromGlobal(globalX, globalY)
        const members = dockState.panelsInDock(dockId, false).filter(function(id) {
            return id !== root.dockState.draggedPanelId
        })
        for (let index = 0; index < tabRepeater.count; ++index) {
            const item = tabRepeater.itemAt(index)
            if (item && point.x < item.x + item.width / 2) {
                const position = members.indexOf(item.modelData)
                if (position >= 0) return position
            }
        }
        return members.length
    }

    function panelLabel(panelId) {
        const definition = dockState.panelDefinitions.filter(function(panel) { return panel.id === panelId })[0]
        return definition ? definition.label : panelId
    }

    function moveActivePanel(destination) {
        const id = activePanelId
        if (!id || !dockState.movePanel(id, destination, 999))
            return false
        panelMoveRequested(id, destination)
        return true
    }

    function toggleFloatActivePanel() {
        const id = activePanelId
        if (!id)
            return false
        const shouldFloat = dockState.dock(id) !== "floating"
        const changed = shouldFloat ? dockState.floatPanel(id) : dockState.dockPanel(id)
        if (changed)
            panelFloatRequested(id, shouldFloat)
        return changed
    }

    function focusPanelTab(panelId) {
        const id = String(panelId || "")
        if (!dockState.focusPanel(id))
            return false
        panelFocused(id)
        revealPanel(id)
        return true
    }

    function revealPanel(panelId) {
        const panelIndex = visiblePanelIds.indexOf(String(panelId || ""))
        if (panelIndex < 0)
            return false
        const tab = tabRepeater.itemAt(panelIndex)
        if (!tab || tab.width <= 0 || tabViewport.width <= 0)
            return false
        const tabPosition = tab.mapToItem(tabRow, 0, 0)
        const tabLeft = tabPosition.x
        const tabRight = tabLeft + tab.width
        let nextContentX = tabViewport.contentX
        if (tabLeft < nextContentX)
            nextContentX = tabLeft
        else if (tabRight > nextContentX + tabViewport.width)
            nextContentX = tabRight - tabViewport.width
        const maximum = Math.max(0, tabViewport.contentWidth - tabViewport.width)
        tabViewport.contentX = Math.max(0, Math.min(nextContentX, maximum))
        return true
    }

    function revealActivePanelTab() {
        Qt.callLater(function() { root.revealPanel(root.activePanelId) })
    }

    onActivePanelIdChanged: revealActivePanelTab()
    onVisiblePanelIdsChanged: revealActivePanelTab()

    component DockMoveMenuItem: MenuItem {
        id: moveItem
        property string destinationDock: ""
        contentItem: Text {
            text: moveItem.text
            color: moveItem.highlighted ? root.theme.textStrong : root.theme.textMain
            font.family: root.theme.fontUi
            font.pixelSize: root.theme.typeBody
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: moveItem.highlighted ? root.theme.actionSecondaryHover : "transparent"
        }
        onTriggered: root.moveActivePanel(destinationDock)
    }

    Rectangle {
        objectName: "timelineDockDropTarget_" + root.dockId
        anchors.fill: parent
        z: 1000
        visible: root.dockState.draggedPanelId.length > 0 && root.dockState.dragTargetDock === root.dockId
        color: root.theme.alpha(root.theme.focusRing, 0.08)
        border.color: root.theme.focusRing
        border.width: root.theme.focusBorderWidth
    }

    Item {
        id: headerHost
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.theme.space8
        anchors.rightMargin: root.theme.space8
        anchors.topMargin: root.theme.space6
        implicitHeight: root.theme.controlHeightDense
        height: implicitHeight

        RowLayout {
            id: headerRow
            anchors.fill: parent
            spacing: root.theme.gapDense

            Flickable {
                id: tabViewport
                objectName: "timelineDockTabViewport_" + root.dockId
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                interactive: contentWidth > width
                contentWidth: tabRow.implicitWidth
                contentHeight: height

                onWidthChanged: root.revealActivePanelTab()
                onContentWidthChanged: root.revealActivePanelTab()

                ScrollBar.horizontal: ScrollBar {
                    height: root.theme.scaled(4)
                    policy: tabViewport.contentWidth > tabViewport.width
                            ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                Row {
                    id: tabRow
                    height: tabViewport.height
                    width: implicitWidth
                    spacing: root.theme.gapDense

                    Label {
                        visible: root.visiblePanelIds.length === 0 && root.title.length > 0
                        text: root.title
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                        verticalAlignment: Text.AlignVCenter
                        height: root.theme.controlHeightDense
                    }

                    Repeater {
                        id: tabRepeater
                        model: root.visiblePanelIds

                        delegate: Item {
                            id: panelTab
                            required property string modelData
                            objectName: "timelineDockTabItem_" + modelData
                            readonly property bool selected: root.activePanelId === modelData
                            readonly property bool closeVisible: false
                            width: tabButton.implicitWidth
                            height: tabViewport.height

                            HoverHandler {
                                id: tabHover
                            }

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                height: root.theme.controlHeightDense
                                spacing: root.theme.space2

                                NavigationTab {
                                    id: tabButton
                                    objectName: "timelineDockTab_" + panelTab.modelData
                                    // The tab lives in a dense clipped header; keep its
                                    // underline and keyboard focus inside that header.
                                    implicitHeight: root.theme.controlHeightDense
                                    text: root.panelLabel(panelTab.modelData)
                                    compact: true
                                    tone: "quiet"
                                    selected: panelTab.selected
                                    underlineSelection: true
                                    showText: true
                                    accessibleName: text + " panel tab"
                                    tooltipText: moveMenu.visible ? "" : "Show " + text + ". Drag to rearrange; right-click for panel actions."
                                    onClicked: root.focusPanelTab(panelTab.modelData)
                                    MouseArea {
                                        id: tabDragMouseArea
                                        objectName: "timelineDockTabDrag_" + panelTab.modelData
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        preventStealing: true
                                        cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                        property bool dragging: false
                                        property point pressPoint: Qt.point(0, 0)
                                        onPressed: function(mouse) {
                                            tabButton.forceActiveFocus(Qt.MouseFocusReason)
                                            pressPoint = mapToGlobal(mouse.x, mouse.y)
                                            if (mouse.button === Qt.RightButton) {
                                                root.focusPanelTab(panelTab.modelData)
                                                const point = mapToItem(moveButton, mouse.x, mouse.y)
                                                moveMenu.x = point.x
                                                moveMenu.y = point.y
                                            }
                                        }
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.RightButton) {
                                                const point = mapToItem(moveButton, mouse.x, mouse.y)
                                                moveMenu.x = point.x
                                                moveMenu.y = point.y
                                                moveMenu.toggleFrom(tabButton, tabDragMouseArea)
                                            }
                                        }
                                        onPositionChanged: function(mouse) {
                                            if (!(pressedButtons & Qt.LeftButton)) return
                                            const point = mapToGlobal(mouse.x, mouse.y)
                                            if (!dragging && Math.hypot(point.x - pressPoint.x, point.y - pressPoint.y) > root.theme.scaled(6)) {
                                                dragging = root.dockState.beginPanelDrag(panelTab.modelData)
                                            }
                                            if (dragging) root.dockState.updatePanelDrag(point.x, point.y)
                                        }
                                        onReleased: function(mouse) {
                                            if (mouse.button !== Qt.LeftButton) return
                                            const point = mapToGlobal(mouse.x, mouse.y)
                                            if (dragging) root.dockState.finishPanelDrag(point.x, point.y)
                                            else root.focusPanelTab(panelTab.modelData)
                                            dragging = false
                                        }
                                        onCanceled: { dragging = false; root.dockState.cancelPanelDrag() }
                                    }
                                    onActiveFocusChanged: {
                                        if (activeFocus)
                                            root.revealPanel(panelTab.modelData)
                                    }
                                }

                                ActionButton {
                                    id: closeButton
                                    objectName: "timelineDockClose_" + panelTab.modelData
                                    iconName: "close"
                                    compact: true
                                    tone: "quiet"
                                    embedded: true
                                    showText: false
                                    visible: panelTab.closeVisible
                                    enabled: visible
                                    accessibleName: "Close " + root.panelLabel(panelTab.modelData) + " panel"
                                    tooltipText: "Close " + root.panelLabel(panelTab.modelData) + " panel"
                                    onClicked: {
                                        root.dockState.setVisible(panelTab.modelData, false)
                                        root.panelClosed(panelTab.modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ActionButton {
                id: moveButton
                objectName: "timelineDockMoveButton_" + root.dockId
                Layout.preferredWidth: root.theme.controlHeightDense
                Layout.preferredHeight: root.theme.controlHeightDense
                text: "Panel options"
                iconName: "more"
                compact: true
                tone: "quiet"
                showText: false
                enabled: root.activePanelId.length > 0
                accessibleName: "Options for " + root.panelLabel(root.activePanelId) + " panel"
                tooltipText: "Panel options. Drag tabs to rearrange; right-click a tab for more actions."
                onClicked: moveMenu.toggleFrom(moveButton)

                AppMenu {
                    id: moveMenu
                    objectName: "timelineDockMoveMenu_" + root.dockId
                    transientMenu: true
                    MenuItem {
                        objectName: "timelineDockMenuClose_" + root.dockId
                        text: "Close " + root.panelLabel(root.activePanelId)
                        onTriggered: {
                            const panelId = root.activePanelId
                            root.dockState.setVisible(panelId, false)
                            root.panelClosed(panelId)
                        }
                    }
                    MenuItem {
                        objectName: "timelineDockMenuFloat_" + root.dockId
                        text: root.dockId === "floating" ? "Dock panel" : "Detach panel"
                        onTriggered: root.toggleFloatActivePanel()
                    }
                    MenuSeparator {}
                    DockMoveMenuItem {
                        objectName: "timelineDockMove_left"
                        text: "Left dock"
                        destinationDock: "left"
                    }
                    DockMoveMenuItem {
                        objectName: "timelineDockMove_right"
                        text: "Right dock"
                        destinationDock: "right"
                    }
                    DockMoveMenuItem {
                        objectName: "timelineDockMove_auxiliary"
                        text: "Auxiliary dock"
                        destinationDock: "auxiliary"
                    }
                    DockMoveMenuItem {
                        objectName: "timelineDockMove_bottom"
                        text: "Bottom dock"
                        destinationDock: "bottom"
                    }
                }
            }

            ActionButton {
                id: floatButton
                visible: false
                objectName: "timelineDockFloatButton_" + root.dockId
                Layout.preferredWidth: root.theme.controlHeightDense
                Layout.preferredHeight: root.theme.controlHeightDense
                text: root.activePanelId && root.dockState.dock(root.activePanelId) === "floating"
                      ? "Dock" : "Float"
                iconName: root.activePanelId && root.dockState.dock(root.activePanelId) === "floating"
                          ? "panel-right" : "external-link"
                compact: true
                tone: "quiet"
                showText: false
                enabled: root.activePanelId.length > 0
                accessibleName: text + " " + root.panelLabel(root.activePanelId) + " panel"
                tooltipText: text + " " + root.panelLabel(root.activePanelId) + " panel"
                onClicked: root.toggleFloatActivePanel()
            }
        }
    }

    Item {
        id: panelContentHost
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerHost.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.theme.space8
        anchors.rightMargin: root.theme.space8
        anchors.topMargin: root.theme.space4
        anchors.bottomMargin: root.theme.space8
        clip: true
    }
}
