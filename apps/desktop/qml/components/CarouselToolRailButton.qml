import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: root

    property var toolData: ({})
    property string activeTool: ""
    property var menuItems: []
    property int menuWidth: 238
    property bool compactLabelMode: false
    readonly property int menuMinimumWidth: theme.menuMinWidth
    readonly property int menuMaximumWidth: theme.menuMaxWidth
    readonly property int menuPopoverPadding: theme.space6

    signal toolSelected(string toolValue)
    signal menuItemTriggered(string toolValue, string itemKey)

    Theme { id: theme }

    readonly property string carouselToolRailButtonContract: "carousel-tool-rail-button:v2|stable-symbol:true|active-fill-marker:true|shortcut-tooltip:true|accessible-name-state:true|compact-label:true|rail-width:48|expandable-menu:true|checked-state:true|disabled-reasons:true|keyboard:true|readable-text:true|no-elide:true|geometry:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|tooltip-full:true|menu-wrap:true|accessibility-full:true"
    readonly property string menuGeometryContract: "menu-geometry:v1|min-width:" +
                                                   String(root.menuMinimumWidth) +
                                                   "|max-width:" +
                                                   String(root.menuMaximumWidth) +
                                                   "|padding:" +
                                                   String(root.menuPopoverPadding) +
                                                   "|item-height:32-36|icon-column:true|shortcut-column:true|labels-wrap:true"
    readonly property string toolValue: String(toolData.value || "")
    readonly property string toolLabel: String(toolData.label || toolValue)
    readonly property string toolKey: String(toolData.key || "")
    readonly property bool toolActive: activeTool === toolValue

    function toolIconName(value) {
        if (value === "select")
            return "select"
        if (value === "hand")
            return "hand"
        if (value === "text")
            return "text-tool"
        if (value === "media")
            return "media"
        if (value === "shape")
            return "shape-tool"
        if (value === "brush")
            return "brush-tool"
        return ""
    }

    function toolGlyph(value) {
        if (value === "hand")
            return "H"
        if (value === "shape")
            return "U"
        if (value === "text")
            return "T"
        if (value === "brush")
            return "B"
        return ""
    }

    Layout.fillWidth: true
    objectName: "carouselToolRail" + toolValue.charAt(0).toUpperCase() +
                toolValue.slice(1) + "Button"
    padding: 0
    implicitHeight: 40
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: toolLabel + " tool, shortcut " + toolKey
    Accessible.description: toolActive
                            ? "Selected tool. Open the menu for variants and related actions."
                            : "Press to select. Open the menu for variants and related actions."
    Accessible.role: Accessible.Button
    Accessible.checked: toolActive
    ToolTip.visible: hovered && !carouselToolMenu.visible
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: toolLabel + " (" + toolKey + ") - menu has variants and related actions"

    onClicked: root.toolSelected(toolValue)
    onPressAndHold: carouselToolMenu.open()

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        hoverEnabled: false
        onClicked: carouselToolMenu.open()
    }

    background: CommandButtonSurface {
        cornerRadius: theme.radiusControl
        hovered: root.hovered
        pressed: root.down
        selected: root.toolActive
        interactiveEnabled: root.enabled
        fillColor: theme.commandFill("secondary", root.toolActive, root.hovered, root.down,
                                     root.enabled, true)
        borderWidth: theme.commandBorderWidth(root.visualFocus)
        borderColor: root.visualFocus ? theme.focusRing : theme.controlEdge
    }

    contentItem: Item {
        AppIcon {
            visible: root.toolIconName(root.toolValue).length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: root.compactLabelMode ? 5 : Math.round((parent.height - height) / 2)
            width: theme.scaled(18)
            height: width
            name: root.toolIconName(root.toolValue)
            color: root.toolActive ? theme.accentWarm300 : theme.textMain
        }

        Text {
            visible: root.toolIconName(root.toolValue).length === 0
            anchors.centerIn: parent
            text: root.toolGlyph(root.toolValue)
            color: root.toolActive ? theme.accentWarm300 : theme.textMain
            font.family: theme.fontMono
            font.pixelSize: 13
            font.weight: Font.Bold
            maximumLineCount: 1
            elide: Text.ElideNone
        }

        Text {
            visible: root.compactLabelMode
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.rightMargin: root.compactLabelMode ? theme.scaled(14) : 0
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 3
            text: root.toolLabel
            color: root.toolActive ? theme.accentWarm300 : theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
            font.weight: root.toolActive ? Font.DemiBold : Font.Normal
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: theme.typeMicro
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 1
            elide: Text.ElideNone
        }

        Rectangle {
            visible: root.hovered || carouselToolMenu.visible
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 3
            width: 10
            height: 10
            radius: 2
            color: carouselToolMenu.visible
                   ? theme.alpha(theme.accentWarm400, 0.30)
                   : root.hovered || root.toolActive
                     ? theme.alpha(theme.ink950, 0.42)
                     : "transparent"
            border.width: carouselToolMenu.visible ? 1 : 0
            border.color: theme.alpha(theme.accentWarm300, 0.64)

            AppIcon {
                anchors.centerIn: parent
                width: 7
                height: 7
                name: "menu"
                color: root.toolActive ? theme.accentWarm300 : theme.textMuted
            }
        }

        MouseArea {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 18
            height: 18
            cursorShape: Qt.PointingHandCursor
            onClicked: carouselToolMenu.open()
        }
    }

    Popup {
        id: carouselToolMenu

        property var menuModel: root.menuItems || []
        property int currentFocusIndex: -1

        function itemEnabledAt(index) {
            if (index < 0 || index >= menuModel.length)
                return false
            return menuModel[index].enabled !== false
        }

        function firstEnabledIndex() {
            for (let i = 0; i < menuModel.length; ++i) {
                if (itemEnabledAt(i))
                    return i
            }
            return -1
        }

        function checkedIndex() {
            for (let i = 0; i < menuModel.length; ++i) {
                if (itemEnabledAt(i) && menuModel[i].checked === true)
                    return i
            }
            return -1
        }

        function focusMenuItem(index) {
            if (!itemEnabledAt(index))
                return false
            const item = carouselToolMenuRepeater.itemAt(index)
            if (!item)
                return false
            currentFocusIndex = index
            item.forceActiveFocus(Qt.TabFocusReason)
            return true
        }

        function focusNextMenuItem(step) {
            if (menuModel.length === 0)
                return
            const start = currentFocusIndex >= 0 ? currentFocusIndex : firstEnabledIndex()
            for (let offset = 1; offset <= menuModel.length; ++offset) {
                const index = (start + step * offset + menuModel.length) % menuModel.length
                if (focusMenuItem(index))
                    return
            }
        }

        x: root.width + theme.space8
        y: 0
        width: Math.min(root.menuMaximumWidth,
                        Math.max(root.menuMinimumWidth, root.menuWidth,
                                 carouselToolMenuColumn.implicitWidth + root.menuPopoverPadding * 2))
        modal: false
        focus: true
        padding: root.menuPopoverPadding
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            currentFocusIndex = checkedIndex()
            if (currentFocusIndex < 0)
                currentFocusIndex = firstEnabledIndex()
            if (currentFocusIndex >= 0)
                Qt.callLater(function() { carouselToolMenu.focusMenuItem(currentFocusIndex) })
        }

        contentItem: ColumnLayout {
            id: carouselToolMenuColumn
            spacing: 4
            focus: true
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Down) {
                    carouselToolMenu.focusNextMenuItem(1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Up) {
                    carouselToolMenu.focusNextMenuItem(-1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Home) {
                    carouselToolMenu.focusMenuItem(carouselToolMenu.firstEnabledIndex())
                    event.accepted = true
                } else if (event.key === Qt.Key_End) {
                    for (let i = carouselToolMenu.menuModel.length - 1; i >= 0; --i) {
                        if (carouselToolMenu.focusMenuItem(i))
                            break
                    }
                    event.accepted = true
                }
            }

            Repeater {
                id: carouselToolMenuRepeater
                model: carouselToolMenu.menuModel

                delegate: PopupMenuRow {
                    id: carouselToolMenuItem
                    previousItem: index > 0 ? carouselToolMenu.menuModel[index - 1] : ({})
                    accent: theme.accentWarm400
                    labelMaxLines: 2
                    tooltipTimeout: 3600
                    onFocusIndexRequested: function(itemIndex) { carouselToolMenu.currentFocusIndex = itemIndex }
                    onActivated: function(key) {
                        root.menuItemTriggered(root.toolValue, key)
                        carouselToolMenu.close()
                    }
                    onNavigateRequested: function(direction) { carouselToolMenu.focusNextMenuItem(direction) }
                    onFirstRequested: carouselToolMenu.focusMenuItem(carouselToolMenu.firstEnabledIndex())
                    onLastRequested: {
                        for (let i = carouselToolMenu.menuModel.length - 1; i >= 0; --i) {
                            if (carouselToolMenu.focusMenuItem(i))
                                break
                        }
                    }
                }
            }
        }

        background: PopupSurface {
            accent: theme.accentWarm400
        }
    }
}
