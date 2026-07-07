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
    readonly property string actionZoneContract: "layer-row-action-zone:v3|component:LayerRowActionZone|normal-width:178|brush-width:52|brush:more-only|summary-in-status:true|mask-state:normal-only|opacity-readout:normal-only|visibility:normal-only|more:true|hover-no-shift:true|no-elide:true"

    signal visibilityClicked()
    signal menuItemTriggered(string key)

    Theme { id: theme }

    Layout.preferredWidth: root.brushMode ? 52 : 178
    Layout.minimumWidth: root.brushMode ? 52 : 178
    Layout.maximumWidth: root.brushMode ? 52 : 178
    Layout.preferredHeight: 26
    spacing: 5

    Rectangle {
        objectName: "photoLayerMaskStatePill"
        visible: !root.brushMode
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
        visible: !root.brushMode
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
        iconName: root.layerVisible ? "media" : "delete"
        compact: true
        label: root.layerVisible ? "Hide layer" : "Show layer"
        active: root.layerVisible
        onClicked: root.visibilityClicked()
    }

    Components.MenuButton {
        objectName: "photoLayerRowMoreMenuButton"
        text: "More"
        iconName: "menu"
        compact: true
        width: implicitWidth
        height: implicitHeight
        enabled: root.layerIndex >= 0
        menuItems: root.menuItems
        tooltipText: "Layer actions."
        onMenuItemTriggered: function(key) { root.menuItemTriggered(key) }
    }
}
