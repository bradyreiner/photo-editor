pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

RowLayout {
    id: root
    property string rowKind: "photo-layer"

    property bool brushMode: false
    property bool showEditButton: true
    property bool editActive: false
    property bool showVisibilityButton: true
    property bool visibilityEnabled: true
    property bool showMenuButton: true
    property bool rowHoveredOrFocused: false
    property bool hasMask: false
    property bool maskSelected: false
    property bool layerVisible: true
    property int layerOpacityPercent: 100
    property int layerIndex: -1
    property color accent: theme.accentCool400
    property var menuItems: []
    property var menuItemsProvider: null
    property string visibilityDisabledReason: ""
    readonly property bool editActionVisible: root.showEditButton && !root.brushMode
    readonly property bool actionsRevealed: root.rowHoveredOrFocused || editAction.activeFocus ||
                                            visibilityAction.activeFocus || photoLayerMenuButton.activeFocus
    readonly property string actionZoneContract: "layer-row-action-zone:v8|component:LayerRowActionZone|wide:edit-visibility-more|medium:edit-more|compact:more|brush:more|edit:labeled|summary-in-tooltip:true|mask-state:tooltip-only|opacity-readout:properties-only|visibility:icon-only|more:icon-only|hover-focus-reveal:true|reserved-width:true|no-layout-shift:true|no-elide:true"

    signal editClicked()
    signal visibilityClicked()
    signal menuItemTriggered(string key)

    Theme { id: theme }

    readonly property int editButtonWidth: theme.scaled(58)
    readonly property int visibleActionCount: (root.editActionVisible ? 1 : 0) +
                                               (!root.brushMode && root.showVisibilityButton ? 1 : 0) +
                                               (root.showMenuButton ? 1 : 0)
    Layout.preferredWidth: (root.editActionVisible ? editButtonWidth : 0) +
                           (!root.brushMode && root.showVisibilityButton ? theme.iconButtonSizeCompact : 0) +
                           (root.showMenuButton ? theme.iconButtonSizeCompact : 0) +
                           Math.max(0, visibleActionCount - 1) * theme.space4
    Layout.minimumWidth: Layout.preferredWidth
    Layout.maximumWidth: Layout.preferredWidth
    Layout.preferredHeight: theme.iconButtonSizeCompact
    spacing: theme.space4
    opacity: actionsRevealed ? 1.0 : 0.0
    enabled: actionsRevealed

    Behavior on opacity {
        NumberAnimation { duration: theme.durationFast; easing.type: Easing.OutCubic }
    }

    Components.ToolButton {
        id: editAction
        objectName: "photoLayerRowEditButton"
        Layout.preferredWidth: root.editButtonWidth
        visible: root.editActionVisible
        iconName: "edit"
        compact: true
        showLabel: true
        label: "Edit"
        active: root.editActive
        enabled: root.layerIndex >= 0
        onClicked: root.editClicked()
    }

    Rectangle {
        id: popupTooltipAnchor1
        objectName: "photoLayerMaskStatePill"
        visible: false
        Layout.preferredWidth: 42
        Layout.preferredHeight: 24
        radius: 3
        color: root.hasMask || root.maskSelected
               ? theme.alpha(root.accent, 0.14)
               : theme.alpha(theme.surfaceTop, 0.34)
        border.width: theme.chromeBorderWidth
        border.color: root.hasMask || root.maskSelected
                      ? theme.alpha(root.accent, 0.52)
                      : theme.alpha(theme.textMuted, 0.24)
        AppToolTip {
            id: sharedTooltip2
            targetItem: popupTooltipAnchor1
            visible: popupTooltipAnchor1.visible && (photoLayerMaskStateHover.hovered)
            delay: 300
            text: root.hasMask ? "Layer mask enabled." : "No layer mask."
        }
        HoverHandler { id: photoLayerMaskStateHover }

        Label {
            anchors.fill: parent
            anchors.leftMargin: 5
            anchors.rightMargin: 5
            text: root.hasMask ? "MASK" : "NO"
            color: root.hasMask || root.maskSelected ? root.accent : theme.textMuted
            font.family: theme.fontNumeric
            font.features: theme.numericFontFeatures
            font.pixelSize: 8
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 6
            elide: Text.ElideNone
        }
    }

    Label {
        objectName: "photoLayerOpacityLabel"
        visible: false
        Layout.preferredWidth: 34
        text: root.layerOpacityPercent + "%"
        color: theme.textMain
        font.family: theme.fontNumeric
        font.features: theme.numericFontFeatures
        font.pixelSize: 9
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignRight
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 7
        elide: Text.ElideNone
    }

    Components.ToolButton {
        id: visibilityAction
        objectName: root.rowKind === "base-image" ? "baseImageLayerVisibilityButton" : "photoLayerVisibilityButton"
        visible: !root.brushMode && root.showVisibilityButton
        enabled: root.layerIndex >= 0 && root.visibilityEnabled
        iconName: root.layerVisible ? "visible" : "hidden"
        compact: true
        label: root.layerVisible ? "Hide layer" : "Show layer"
        active: false
        disabledReason: root.visibilityDisabledReason.length > 0
                        ? root.visibilityDisabledReason : "Visibility is unavailable for this layer."
        onClicked: root.visibilityClicked()
    }

    Components.ToolButton {
        id: photoLayerMenuButton
        objectName: "photoLayerRowMoreMenuButton"
        visible: root.showMenuButton
        iconName: "menu"
        compact: true
        label: "Layer actions"
        enabled: root.layerIndex >= 0
        onClicked: photoLayerRowMenu.toggleFrom(photoLayerMenuButton)
    }

    LayerActionsMenu {
        id: photoLayerRowMenu
        objectName: "photoLayerRowActionsPopup"
        menuItems: root.menuItems
        menuItemsProvider: root.menuItemsProvider
        onMenuItemTriggered: function(key) { root.menuItemTriggered(key) }
    }
}
