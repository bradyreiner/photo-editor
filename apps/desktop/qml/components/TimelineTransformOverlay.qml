pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    required property var theme
    property bool active: false
    property bool locked: false
    property real sourceAspect: 1
    property string frameMode: "fit"
    property real frameScale: 1
    property real offsetX: 0
    property real offsetY: 0
    property real frameRotation: 0
    property real groupScale: 1
    property real groupRotation: 0
    property real groupOffsetX: 0
    property real groupOffsetY: 0
    property bool interactionActive: false
    property bool gestureChanged: false
    signal interactionStarted()
    signal interactionFinished()
    signal interactionCancelled()
    signal transformEdited(real offsetX, real offsetY, real scale, real rotation)

    visible: active && width > 1 && height > 1
    enabled: visible && !locked
    readonly property real innerWidth: Math.max(1, width - 18)
    readonly property real innerHeight: Math.max(1, height - 18)
    readonly property real baseWidth: frameMode === "fit"
        ? Math.min(innerWidth, innerHeight * Math.max(0.01, sourceAspect)) : innerWidth
    readonly property real baseHeight: frameMode === "fit"
        ? Math.min(innerHeight, innerWidth / Math.max(0.01, sourceAspect)) : innerHeight
    readonly property real radians: groupRotation * Math.PI / 180
    readonly property real centerX: width / 2 + groupOffsetX * width + groupScale
        * (Math.cos(radians) * offsetX * width / 4 - Math.sin(radians) * offsetY * height / 4)
    readonly property real centerY: height / 2 + groupOffsetY * height + groupScale
        * (Math.sin(radians) * offsetX * width / 4 + Math.cos(radians) * offsetY * height / 4)

    QtObject {
        id: gesture
        property point pointer: Qt.point(0, 0)
        property point center: Qt.point(0, 0)
        property point anchor: Qt.point(0, 0)
        property point diagonal: Qt.point(0, 0)
        property real scale: 1
        property real rotation: 0
        property real angle: 0
        property real x: 0
        property real y: 0
        property real nextScale: 1
        property real nextRotation: 0
        property real nextX: 0
        property real nextY: 0
        property string mode: "move"
    }

    function begin(mode, point, cornerX, cornerY) {
        forceActiveFocus()
        interactionActive = true
        gestureChanged = false
        gesture.mode = mode
        gesture.pointer = point
        gesture.center = Qt.point(centerX, centerY)
        gesture.scale = frameScale
        gesture.rotation = frameRotation
        gesture.x = offsetX
        gesture.y = offsetY
        gesture.angle = Math.atan2(point.y - centerY, point.x - centerX)
        const angle = (frameRotation + groupRotation) * Math.PI / 180
        const dx = cornerX * baseWidth * frameScale * groupScale
        const dy = cornerY * baseHeight * frameScale * groupScale
        gesture.diagonal = Qt.point(Math.cos(angle) * dx - Math.sin(angle) * dy,
                                    Math.sin(angle) * dx + Math.cos(angle) * dy)
        gesture.anchor = Qt.point(centerX - gesture.diagonal.x / 2,
                                  centerY - gesture.diagonal.y / 2)
    }

    function update(point, modifiers) {
        if (!interactionActive) return
        let dx = point.x - gesture.pointer.x
        let dy = point.y - gesture.pointer.y
        if (!gestureChanged && Math.hypot(dx, dy) < 2) return
        if (!gestureChanged) {
            gestureChanged = true
            interactionStarted()
        }
        let center = gesture.center
        let scale = gesture.scale
        let rotation = gesture.rotation
        if (gesture.mode === "move") {
            if (modifiers & Qt.ShiftModifier) {
                if (Math.abs(dx) >= Math.abs(dy)) dy = 0
                else dx = 0
            }
            center = Qt.point(gesture.center.x + dx, gesture.center.y + dy)
            if (!(modifiers & Qt.AltModifier)) {
                if (Math.abs(center.x - width / 2) < 6) center.x = width / 2
                if (Math.abs(center.y - height / 2) < 6) center.y = height / 2
            }
        } else if (gesture.mode === "resize") {
            const vx = gesture.diagonal.x + dx
            const vy = gesture.diagonal.y + dy
            const length = Math.max(1, gesture.diagonal.x ** 2 + gesture.diagonal.y ** 2)
            scale = Math.max(0.05, Math.min(4, gesture.scale
                * (vx * gesture.diagonal.x + vy * gesture.diagonal.y) / length))
            center = Qt.point(gesture.anchor.x + gesture.diagonal.x * scale / gesture.scale / 2,
                              gesture.anchor.y + gesture.diagonal.y * scale / gesture.scale / 2)
        } else {
            const angle = Math.atan2(point.y - gesture.center.y, point.x - gesture.center.x)
            const delta = ((angle - gesture.angle) * 180 / Math.PI + 540) % 360 - 180
            rotation = Math.max(-180, Math.min(180, gesture.rotation + delta))
            if (modifiers & Qt.ShiftModifier) rotation = Math.round(rotation / 15) * 15
        }
        const gx = center.x - width / 2 - groupOffsetX * width
        const gy = center.y - height / 2 - groupOffsetY * height
        gesture.nextX = Math.max(-4, Math.min(4,
            (Math.cos(radians) * gx + Math.sin(radians) * gy) / Math.max(0.01, groupScale) * 4 / width))
        gesture.nextY = Math.max(-4, Math.min(4,
            (-Math.sin(radians) * gx + Math.cos(radians) * gy) / Math.max(0.01, groupScale) * 4 / height))
        gesture.nextScale = scale
        gesture.nextRotation = rotation
        if (!publishTimer.running) publishTimer.start()
    }

    function flush() {
        publishTimer.stop()
        if (gestureChanged)
            transformEdited(gesture.nextX, gesture.nextY, gesture.nextScale, gesture.nextRotation)
    }

    function finish(cancelled) {
        if (!interactionActive) return
        if (!cancelled) flush()
        else publishTimer.stop()
        interactionActive = false
        if (gestureChanged) {
            if (cancelled) interactionCancelled()
            else interactionFinished()
        }
        gestureChanged = false
    }

    Timer { id: publishTimer; interval: 16; onTriggered: root.flush() }
    Keys.onEscapePressed: function(event) { root.finish(true); event.accepted = true }
    onActiveChanged: if (!active) finish(false)

    Item {
        id: frame
        objectName: "timelineProgramTransformFrame"
        x: root.centerX - width / 2
        y: root.centerY - height / 2
        width: root.baseWidth * root.frameScale * root.groupScale
        height: root.baseHeight * root.frameScale * root.groupScale
        rotation: root.frameRotation + root.groupRotation

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 1
            border.color: root.locked ? root.theme.textMuted : root.theme.accentCool400
        }
        MouseArea {
            objectName: "timelineProgramTransformMoveArea"
            anchors.fill: parent
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            onPressed: function(mouse) { root.begin("move", mapToItem(root, mouse.x, mouse.y), 0, 0) }
            onPositionChanged: function(mouse) { if (pressed) root.update(mapToItem(root, mouse.x, mouse.y), mouse.modifiers) }
            onReleased: root.finish(false)
            onCanceled: root.finish(true)
        }
        Repeater {
            model: [Qt.point(-1, -1), Qt.point(1, -1), Qt.point(-1, 1), Qt.point(1, 1)]
            delegate: Rectangle {
                id: corner
                required property point modelData
                required property int index
                objectName: "timelineProgramResizeHandle" + index
                width: 9; height: 9; radius: 1
                x: (modelData.x + 1) * frame.width / 2 - width / 2
                y: (modelData.y + 1) * frame.height / 2 - height / 2
                color: root.theme.textStrong
                border.color: root.theme.ink950
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -7
                    cursorShape: corner.modelData.x === corner.modelData.y ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                    onPressed: function(mouse) {
                        root.begin("resize", mapToItem(root, mouse.x, mouse.y), corner.modelData.x, corner.modelData.y)
                    }
                    onPositionChanged: function(mouse) { if (pressed) root.update(mapToItem(root, mouse.x, mouse.y), mouse.modifiers) }
                    onReleased: root.finish(false)
                    onCanceled: root.finish(true)
                }
            }
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: frame.y >= 32 ? -22 : 0
            width: 1; height: frame.y >= 32 ? 22 : 17
            color: root.theme.accentCool400
        }
        Rectangle {
            objectName: "timelineProgramRotationHandle"
            anchors.horizontalCenter: parent.horizontalCenter
            y: frame.y >= 32 ? -29 : 12
            width: 10; height: 10; radius: 5
            color: root.theme.textStrong
            MouseArea {
                anchors.fill: parent
                anchors.margins: -7
                cursorShape: Qt.CrossCursor
                onPressed: function(mouse) { root.begin("rotate", mapToItem(root, mouse.x, mouse.y), 0, 0) }
                onPositionChanged: function(mouse) { if (pressed) root.update(mapToItem(root, mouse.x, mouse.y), mouse.modifiers) }
                onReleased: root.finish(false)
                onCanceled: root.finish(true)
            }
        }
    }
}
