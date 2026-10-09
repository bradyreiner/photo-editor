import QtQuick
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
    property bool primaryEnabled: true
    property bool menuEnabled: (menuItems || []).length > 0
    property var menuItems: []
    property string tooltipText: ""
    property string disabledReason: ""
    property string menuTooltipText: text.length > 0 ? "More options for " + text : "More options"
    property string accessibleName: ""
    property int menuWidth: theme.menuMinWidth
    property int menuItemMaxLines: 2
    readonly property int menuMinimumWidth: theme.menuMinWidth
    readonly property int menuMaximumWidth: theme.menuMaxWidth
    readonly property int menuPopoverPadding: theme.space6

    signal clicked()
    signal menuItemTriggered(string key)

    readonly property string resolvedAccessibleName: {
        const explicitName = String(root.accessibleName || "").trim()
        if (explicitName.length > 0)
            return explicitName
        const visibleText = String(root.text || "").trim()
        if (visibleText.length > 0)
            return visibleText
        const helpText = String(root.tooltipText || "").trim()
        return helpText.length > 0 ? helpText : "Options"
    }

    Theme { id: theme }
    MenuItemMetrics { id: menuMetrics; items: root.menuItems }

    implicitHeight: root.tone === "primary" || root.tone === "danger"
                    ? theme.controlHeightPrimary
                    : (compact ? theme.controlHeightDense : theme.controlHeight)
    implicitWidth: primaryButton.implicitWidth + (root.menuEnabled
                  ? Math.max(root.implicitHeight, chevronButton.implicitWidth) + 1
                  : 0)
    opacity: root.enabled ? 1.0 : 0.56
    activeFocusOnTab: false
    Accessible.ignored: true

    Keys.onSpacePressed: function(event) {
        if (root.enabled && root.primaryEnabled)
            root.clicked()
        event.accepted = true
    }
    Keys.onReturnPressed: function(event) {
        if (root.enabled && root.primaryEnabled)
            root.clicked()
        event.accepted = true
    }

    Behavior on opacity {
        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }
    }

    readonly property int checkedItemCount: menuMetrics.checkedItemCount
    readonly property int disabledItemCount: menuMetrics.disabledItemCount
    readonly property int disabledItemReasonCount: menuMetrics.disabledItemReasonCount
    readonly property int dangerItemCount: menuMetrics.dangerItemCount
    readonly property bool disabledItemsExplainState: menuMetrics.disabledItemsExplainState
    readonly property bool destructiveItemsSeparated: menuMetrics.destructiveItemsSeparated
    readonly property bool destructiveItemsLast: menuMetrics.destructiveItemsLast
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|primary-fit:true|menu-wrap:true|menu-lines:" +
                                                   String(root.menuItemMaxLines) +
                                                   "|popover-width:min-content|tooltip-full:true"
    readonly property string menuGeometryContract: "menu-geometry:v1|min-width:" +
                                                   String(root.menuMinimumWidth) +
                                                   "|max-width:" +
                                                   String(root.menuMaximumWidth) +
                                                   "|padding:" +
                                                   String(root.menuPopoverPadding) +
                                                   "|item-height:menu-token|icon-column:true|shortcut-column:true|labels-wrap:true"
    readonly property string disabledStateMotionContract: "disabled-state-motion:v1|duration:110|opacity:true|layout-stable:true|tooltip-preserved:true|interruptible:true"
    readonly property string splitContract: "split-button:v1|primary:" +
                                            String(root.primaryEnabled) +
                                            "|items:" +
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
                                            String(menuWidth) +
                                            "|wrapper:ignored|single-focus-stop:true|accessible-name-override:true"

    RowLayout {
        anchors.fill: parent
        spacing: 1

        ActionButton {
            id: primaryButton
            objectName: root.objectName.length > 0 ? root.objectName + "PrimaryAction" : ""
            Layout.fillWidth: true
            Layout.preferredHeight: root.implicitHeight
            text: root.text
            iconName: root.iconName
            tone: root.tone
            accent: root.accent
            compact: root.compact
            allowLabelWrap: root.allowLabelWrap
            accessibleName: root.resolvedAccessibleName
            active: root.active
            enabled: root.enabled && root.primaryEnabled
            tooltipText: !root.primaryEnabled && root.disabledReason.length > 0
                         ? root.disabledReason
                         : root.tooltipText
            onClicked: root.clicked()
        }

        ActionButton {
            id: chevronButton
            objectName: root.objectName.length > 0 ? root.objectName + "MenuAction" : ""
            visible: root.menuEnabled
            Layout.preferredWidth: root.implicitHeight
            Layout.preferredHeight: root.implicitHeight
            showText: false
            iconName: "chevron-right"
            tone: root.tone
            accent: root.accent
            compact: root.compact
            accessibleName: root.menuTooltipText
            active: menuPopup.visible
            enabled: root.enabled && root.menuEnabled
            tooltipText: root.menuTooltipText
            onClicked: menuPopup.open()

            contentItem.rotation: 90
        }
    }

    MenuPopover {
        id: menuPopup
        ownerObjectName: root.objectName
        menuItems: root.menuItems
        accent: root.accent
        anchorHeight: root.height
        anchorWidth: root.width
        menuWidth: root.menuWidth
        menuItemMaxLines: root.menuItemMaxLines
        menuMinimumWidth: root.menuMinimumWidth
        menuMaximumWidth: root.menuMaximumWidth
        menuPopoverPadding: root.menuPopoverPadding
        strongSurface: true
        onItemTriggered: function(key) { root.menuItemTriggered(key) }
    }
}
