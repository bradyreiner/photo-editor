pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

RowLayout {
    id: root

    property bool brushMode: false
    property bool hasMask: false
    property bool maskSelected: false
    property bool layerVisible: true
    property int layerOpacityPercent: 100
    property int layerIndex: -1
    property color accent: theme.accentCool400
    property var menuItems: []
    readonly property string actionZoneContract: "layer-row-action-zone:v4|component:LayerRowActionZone|normal-width:60|brush-width:30|brush:more-only|summary-in-tooltip:true|mask-state:tooltip-only|opacity-readout:properties-only|visibility:icon-only|more:icon-only|hover-no-shift:true|no-elide:true"

    signal visibilityClicked()
    signal menuItemTriggered(string key)

    Theme { id: theme }

    Layout.preferredWidth: root.brushMode ? 30 : 60
    Layout.minimumWidth: root.brushMode ? 30 : 60
    Layout.maximumWidth: root.brushMode ? 30 : 60
    Layout.preferredHeight: 28
    spacing: 4

    Rectangle {
        objectName: "photoLayerMaskStatePill"
        visible: false
        Layout.preferredWidth: 42
        Layout.preferredHeight: 24
        radius: 3
        color: root.hasMask || root.maskSelected
               ? theme.alpha(root.accent, 0.14)
               : theme.alpha(theme.panelLift, 0.34)
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
        iconName: root.layerVisible ? "visible" : "media"
        compact: true
        label: root.layerVisible ? "Hide layer" : "Show layer"
        active: root.layerVisible
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

    Menu {
        id: photoLayerRowMenu

        Instantiator {
            model: root.menuItems || []

            delegate: MenuItem {
                required property var modelData

                text: String(modelData.label || "")
                enabled: modelData.enabled !== false
                checkable: modelData.checked !== undefined
                checked: modelData.checked === true
                onTriggered: root.menuItemTriggered(String(modelData.key || ""))
            }

            onObjectAdded: function(index, object) { photoLayerRowMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { photoLayerRowMenu.removeItem(object) }
        }
    }
}
