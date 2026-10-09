pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

RowLayout {
    id: root

    property bool brushMode: false
    property bool showEditButton: true
    property bool hasMask: false
    property bool maskSelected: false
    property bool layerVisible: true
    property int layerOpacityPercent: 100
    property int layerIndex: -1
    property color accent: theme.accentCool400
    property var menuItems: []
    readonly property string actionZoneContract: "layer-row-action-zone:v6|component:LayerRowActionZone|normal-width:edit-visibility-more|brush-width:edit-more|edit:labeled|summary-in-tooltip:true|mask-state:tooltip-only|opacity-readout:properties-only|visibility:icon-only|more:icon-only|hover-no-shift:true|no-elide:true"

    signal editClicked()
    signal visibilityClicked()
    signal menuItemTriggered(string key)

    Theme { id: theme }

    readonly property int editButtonWidth: theme.scaled(58)
    readonly property int visibleActionCount: (root.showEditButton ? 1 : 0) +
                                               (root.brushMode ? 0 : 1) + 1
    Layout.preferredWidth: (root.showEditButton ? editButtonWidth : 0) +
                           (root.brushMode ? 0 : theme.iconButtonSizeCompact) +
                           theme.iconButtonSizeCompact +
                           Math.max(0, visibleActionCount - 1) * theme.space4
    Layout.minimumWidth: Layout.preferredWidth
    Layout.maximumWidth: Layout.preferredWidth
    Layout.preferredHeight: theme.iconButtonSizeCompact
    spacing: theme.space4

    Components.ToolButton {
        objectName: "photoLayerRowEditButton"
        Layout.preferredWidth: root.editButtonWidth
        visible: root.showEditButton
        iconName: "edit"
        compact: true
        showLabel: true
        label: "Edit"
        active: false
        enabled: root.layerIndex >= 0
        onClicked: root.editClicked()
    }

    Rectangle {
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
        ToolTip.visible: photoLayerMaskStateHover.hovered
        ToolTip.delay: 300
        ToolTip.text: root.hasMask ? "Layer mask enabled." : "No layer mask."

        HoverHandler { id: photoLayerMaskStateHover }

        Label {
            anchors.fill: parent
            anchors.leftMargin: 5
            anchors.rightMargin: 5
            text: root.hasMask ? "MASK" : "NO"
            color: root.hasMask || root.maskSelected ? root.accent : theme.textMuted
            font.family: theme.fontMono
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
        font.family: theme.fontMono
        font.pixelSize: 9
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignRight
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 7
        elide: Text.ElideNone
    }

    Components.ToolButton {
        objectName: "photoLayerVisibilityButton"
        visible: !root.brushMode
        iconName: root.layerVisible ? "visible" : "hidden"
        compact: true
        label: root.layerVisible ? "Hide layer" : "Show layer"
        active: false
        onClicked: root.visibilityClicked()
    }

    Components.ToolButton {
        objectName: "photoLayerRowMoreMenuButton"
        iconName: "menu"
        compact: true
        label: "Layer actions"
        enabled: root.layerIndex >= 0
        onClicked: photoLayerRowMenu.open()
    }

    LayerActionsMenu {
        id: photoLayerRowMenu
        objectName: "photoLayerRowActionsPopup"
        menuItems: root.menuItems
        onMenuItemTriggered: function(key) { root.menuItemTriggered(key) }
    }
}
