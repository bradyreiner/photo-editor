import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root

    property int assetIndex: -1
    property string assetId: ""
    property string thumbnailSource: ""
    property real thumbnailWidth: 72
    property real thumbnailHeight: 54
    property bool current: false
    property bool selected: false

    signal activated(int assetIndex, int modifiers)

    Theme { id: theme }

    readonly property string editFilmstripThumbContract: "edit-filmstrip-thumb:v1|current-state:true|selected-state:true|empty-state:true|keyboard:true|tooltip-full:true|accessibility-full:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|tooltip-full:true|accessibility-full:true|image-only:true"
    readonly property string mediaSurfaceContract: "media-surface:v1|dock-thumbnail:stable|image-dominant:true|selection:single-border|badge-corner:reserved|chrome:quiet"
    readonly property string accessibleLabel: assetIndex >= 0 ? "Filmstrip asset " + (assetIndex + 1) : "Filmstrip asset"

    activeFocusOnTab: true
    Accessible.role: Accessible.ListItem
    Accessible.name: accessibleLabel
    Accessible.selected: root.current || root.selected

    Keys.onReturnPressed: root.activated(root.assetIndex, Qt.NoModifier)
    Keys.onEnterPressed: root.activated(root.assetIndex, Qt.NoModifier)
    Keys.onSpacePressed: root.activated(root.assetIndex, Qt.NoModifier)

    Rectangle {
        anchors.fill: parent
        radius: theme.radius6
        color: root.current
               ? theme.alpha(theme.ink750, 0.52)
               : (thumbMouse.containsMouse || root.activeFocus
                  ? theme.alpha(theme.glassLift, 0.16)
                  : "transparent")
        border.width: root.activeFocus ? 2 : (root.current || root.selected ? 1 : 0)
        border.color: root.activeFocus
                      ? theme.focusRing
                      : (root.current
                         ? theme.alpha(theme.polar, 0.50)
                         : theme.alpha(theme.lineStrong, 0.28))
    }

    Rectangle {
        width: root.thumbnailWidth
        height: root.thumbnailHeight
        anchors.centerIn: parent
        radius: theme.radius6
        color: theme.ink950
        clip: true

        Image {
            anchors.fill: parent
            source: root.thumbnailSource
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            visible: root.thumbnailSource.length > 0
        }

        Rectangle {
            anchors.fill: parent
            color: theme.ink800
            visible: root.thumbnailSource.length === 0
        }

    }

    MouseArea {
        id: thumbMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            root.activated(root.assetIndex, mouse.modifiers)
        }
    }

    AppToolTip {
        id: sharedTooltip1
        targetItem: root
        visible: root.visible && (thumbMouse.containsMouse)
        delay: 350
        timeout: 2600
        text: root.accessibleLabel
    }
}
