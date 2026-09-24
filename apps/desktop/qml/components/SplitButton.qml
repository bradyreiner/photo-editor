import QtQuick
import QtQuick.Controls.Basic
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
    property int menuWidth: theme.menuMinWidth
    property int menuItemMaxLines: 2
    readonly property int menuMinimumWidth: theme.menuMinWidth
    readonly property int menuMaximumWidth: theme.menuMaxWidth
    readonly property int menuPopoverPadding: theme.space6

    signal clicked()
    signal menuItemTriggered(string key)

    Theme { id: theme }
    MenuItemMetrics { id: menuMetrics; items: root.menuItems }

    implicitHeight: root.tone === "primary" || root.tone === "danger"
                    ? theme.controlHeightPrimary
                    : (compact ? Math.max(theme.controlHeightDense, theme.scaled(32))
                               : Math.max(theme.controlHeight, theme.scaled(36)))
    implicitWidth: primaryButton.implicitWidth + (root.menuEnabled ? chevronButton.implicitWidth + 1 : 0)
    opacity: root.enabled ? 1.0 : 0.56
    activeFocusOnTab: true
    Accessible.name: root.text
    Accessible.description: root.primaryEnabled ? root.tooltipText : root.disabledReason
    Accessible.role: Accessible.Button

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
                                                   "|item-height:32-36|icon-column:true|shortcut-column:true|labels-wrap:true"
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
                                            String(menuWidth)

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
            enabled: root.enabled && root.primaryEnabled
            tooltipText: !root.primaryEnabled && root.disabledReason.length > 0
                         ? root.disabledReason
                         : root.tooltipText
            onClicked: root.clicked()
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

    MenuPopover {
        id: menuPopup
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
