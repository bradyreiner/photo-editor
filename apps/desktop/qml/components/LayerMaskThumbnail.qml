pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: root
    property string maskSetId: ""
    property bool vectorMask: false
    property string sourceLayerId: ""
    property string previewSource: ""
    property string maskName: "Layer mask"
    property bool selected: false
    property bool locked: false
    property bool maskEnabled: true
    property bool copyRequested: false
    property bool dragging: false
    property bool tooltipArmed: false
    signal activated()
    Theme { id: theme }
    implicitWidth: 32
    implicitHeight: 32
    radius: theme.radius4
    color: theme.surfaceBottom
    border.width: selected ? 2 : 1
    border.color: selected ? theme.accentCool400 : theme.controlEdge
    opacity: maskEnabled ? 1.0 : 0.55
    Accessible.role: Accessible.Button
    Accessible.name: maskName + (locked ? ", locked; click to inspect" : selected ? ", editing mask" : ", click to edit; drag to another layer")
    activeFocusOnTab: true
    Keys.onSpacePressed: root.activated()
    Keys.onReturnPressed: root.activated()
    Image {
        anchors.fill: parent; anchors.margins: 2
        source: root.previewSource
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        visible: source.toString().length > 0
    }
    AppIcon {
        anchors.centerIn: parent
        width: 17; height: 17
        name: root.vectorMask ? "geometry" : "masks"
        color: root.selected ? theme.accentCool400 : theme.textMain
        visible: root.previewSource.length === 0
    }
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.75
        height: 2
        rotation: -45
        radius: 1
        color: theme.warning
        visible: !root.maskEnabled
    }
    AppToolTip {
        targetItem: root
        visible: pointer.containsMouse && root.tooltipArmed && !root.dragging
        text: root.maskName + (root.maskEnabled ? "" : " (disabled)") +
              (root.locked ? " · Locked. Click to inspect; unlock the mask, layer, and its groups to edit or move."
                           : " · Click to edit mask. Drag to move; Alt/Option-drag to copy.")
    }
    QtObject {
        id: dragPayload
        property string maskSetId: ""
        property string sourceLayerId: ""
        property bool vectorMask: false
        property bool copyRequested: false
    }
    Item {
        id: proxy
        width: root.width; height: root.height
        Drag.active: root.dragging
        Drag.source: dragPayload
        Drag.keys: dragPayload.vectorMask ? ["looksy-vector-mask"] : ["looksy-layer-mask"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        Rectangle {
            anchors.fill: parent
            visible: root.dragging
            color: theme.surfaceTop
            border.color: theme.accentCool400
            radius: theme.radius4
            AppIcon { anchors.centerIn: parent; width: 18; height: 18; name: dragPayload.vectorMask ? "geometry" : "masks"; color: theme.accentCool400 }
        }
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        property point pressScenePosition: Qt.point(0, 0)
        cursorShape: root.locked ? Qt.PointingHandCursor : root.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        drag.target: root.locked ? null : proxy
        drag.onActiveChanged: {
            if (pointer.drag.active) root.dragging = true
        }
        onEntered: root.tooltipArmed = true
        onExited: root.tooltipArmed = false
        onPressed: function(mouse) {
            root.tooltipArmed = false
            root.copyRequested = (mouse.modifiers & Qt.AltModifier) !== 0
            dragPayload.maskSetId = root.maskSetId
            dragPayload.sourceLayerId = root.sourceLayerId
            dragPayload.vectorMask = root.vectorMask
            dragPayload.copyRequested = root.copyRequested
            pressScenePosition = mapToItem(null, mouse.x, mouse.y)
            proxy.x = 0; proxy.y = 0
            root.forceActiveFocus()
        }
        onPositionChanged: function(mouse) {
            const scenePosition = mapToItem(null, mouse.x, mouse.y)
            const passedThreshold = pressed && !root.locked &&
                    Math.hypot(scenePosition.x - pressScenePosition.x,
                               scenePosition.y - pressScenePosition.y) >= Math.max(1, drag.threshold)
            root.copyRequested = (mouse.modifiers & Qt.AltModifier) !== 0
            dragPayload.copyRequested = root.copyRequested
            if (drag.active || passedThreshold) {
                root.dragging = true
                if (!drag.active) {
                    proxy.x = mouse.x - root.width / 2
                    proxy.y = mouse.y - root.height / 2
                }
            }
            else if (!pressed) root.tooltipArmed = true
        }
        onReleased: function(mouse) {
            root.copyRequested = (mouse.modifiers & Qt.AltModifier) !== 0
            dragPayload.copyRequested = root.copyRequested
            if (root.dragging) proxy.Drag.drop()
            else root.activated()
            root.dragging = false
            root.tooltipArmed = false
            proxy.x = 0; proxy.y = 0
        }
        onCanceled: { root.dragging = false; root.tooltipArmed = false; proxy.x = 0; proxy.y = 0 }
    }
}
