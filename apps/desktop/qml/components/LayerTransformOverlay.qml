import QtQuick

Item {
    id: root

    property bool active: false
    property bool locked: false
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property real canvasWidth: 0
    property real canvasHeight: 0
    property real layerX: 0
    property real layerY: 0
    property real layerWidth: 0
    property real layerHeight: 0
    property real layerScale: 1
    property real layerRotation: 0

    signal transformEdited(int x, int y, real scale, real rotation)

    visible: active && canvasWidth > 0 && canvasHeight > 0 && layerWidth > 0 && layerHeight > 0
    enabled: visible && !locked

    readonly property real viewportScaleX: canvasWidth > 0 && imageWidth > 0 ? imageWidth / canvasWidth : 1
    readonly property real viewportScaleY: canvasHeight > 0 && imageHeight > 0 ? imageHeight / canvasHeight : 1
    readonly property real viewportScale: Math.min(viewportScaleX, viewportScaleY)
    readonly property real boundedScale: Math.max(0.05, Math.min(32, layerScale))
    readonly property real frameX: imageX + layerX * viewportScaleX
    readonly property real frameY: imageY + layerY * viewportScaleY
    readonly property real frameWidth: layerWidth * boundedScale * viewportScaleX
    readonly property real frameHeight: layerHeight * boundedScale * viewportScaleY
    readonly property real centerX: frameX + frameWidth / 2
    readonly property real centerY: frameY + frameHeight / 2
    readonly property color guideColor: locked ? "#8f98a2" : "#72cbb2"

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function emitTransform(nextX, nextY, nextScale, nextRotation) {
        transformEdited(Math.round(clamp(nextX, -1000000, 1000000)),
                        Math.round(clamp(nextY, -1000000, 1000000)),
                        clamp(nextScale, 0.05, 32),
                        clamp(nextRotation, -3600, 3600))
    }

    function angleForPoint(point) {
        return Math.atan2(point.y - centerY, point.x - centerX) * 180 / Math.PI
    }

    function distanceForPoint(point) {
        const dx = point.x - centerX
        const dy = point.y - centerY
        return Math.max(1, Math.sqrt(dx * dx + dy * dy))
    }

    function moveByViewportDelta(deltaX, deltaY) {
        if (!enabled)
            return false
        emitTransform(layerX + deltaX / viewportScaleX, layerY + deltaY / viewportScaleY, layerScale, layerRotation)
        return true
    }

    function scaleFromViewportPoint(pointX, pointY) {
        if (!enabled)
            return false
        const startDistance = Math.max(1, Math.sqrt(frameWidth * frameWidth + frameHeight * frameHeight) / 2)
        const nextDistance = distanceForPoint(Qt.point(pointX, pointY))
        emitTransform(layerX, layerY, boundedScale * nextDistance / startDistance, layerRotation)
        return true
    }

    function rotateFromViewportPoint(pointX, pointY) {
        if (!enabled)
            return false
        emitTransform(layerX, layerY, layerScale, angleForPoint(Qt.point(pointX, pointY)) + 90)
        return true
    }

    QtObject {
        id: dragState

        property real startX: 0
        property real startY: 0
        property real startLayerX: 0
        property real startLayerY: 0
        property real startScale: 1
        property real startDistance: 1
        property real startRotation: 0
        property real startAngle: 0
    }

    Item {
        id: frame
        objectName: "photoLayerTransformFrame"
        x: root.frameX
        y: root.frameY
        width: Math.max(1, root.frameWidth)
        height: Math.max(1, root.frameHeight)
        rotation: root.layerRotation
        transformOrigin: Item.Center
        visible: root.visible

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 2
            border.color: root.guideColor
            opacity: root.locked ? 0.48 : 0.88
        }

        MouseArea {
            objectName: "photoLayerTransformMoveHandle"
            anchors.fill: parent
            enabled: root.enabled
            cursorShape: Qt.SizeAllCursor
            onPressed: function(mouse) {
                const point = frame.mapToItem(root, mouse.x, mouse.y)
                dragState.startX = point.x
                dragState.startY = point.y
                dragState.startLayerX = root.layerX
                dragState.startLayerY = root.layerY
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                const point = frame.mapToItem(root, mouse.x, mouse.y)
                root.emitTransform(dragState.startLayerX + (point.x - dragState.startX) / root.viewportScaleX,
                                   dragState.startLayerY + (point.y - dragState.startY) / root.viewportScaleY,
                                   root.layerScale,
                                   root.layerRotation)
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: -42
            width: 1
            height: 34
            color: root.guideColor
            opacity: 0.76
        }

        Rectangle {
            objectName: "photoLayerTransformRotateHandle"
            anchors.horizontalCenter: parent.horizontalCenter
            y: -56
            width: 18
            height: 18
            radius: 9
            color: root.locked ? "#7f8790" : "#e8edf2"
            border.width: 1
            border.color: "#101419"

            MouseArea {
                anchors.fill: parent
                anchors.margins: -9
                enabled: root.enabled
                cursorShape: Qt.CrossCursor
                onPressed: function(mouse) {
                    const point = parent.mapToItem(root, mouse.x, mouse.y)
                    dragState.startRotation = root.layerRotation
                    dragState.startAngle = root.angleForPoint(point)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    const point = parent.mapToItem(root, mouse.x, mouse.y)
                    root.emitTransform(root.layerX,
                                       root.layerY,
                                       root.layerScale,
                                       dragState.startRotation + root.angleForPoint(point) - dragState.startAngle)
                }
            }
        }

        Repeater {
            model: [
                { name: "photoLayerTransformTopLeftHandle", x: 0, y: 0 },
                { name: "photoLayerTransformTopRightHandle", x: 1, y: 0 },
                { name: "photoLayerTransformBottomLeftHandle", x: 0, y: 1 },
                { name: "photoLayerTransformBottomRightHandle", x: 1, y: 1 }
            ]

            Rectangle {
                required property var modelData

                objectName: modelData.name
                x: modelData.x * parent.width - width / 2
                y: modelData.y * parent.height - height / 2
                width: 18
                height: 18
                radius: 3
                color: root.locked ? "#7f8790" : "#e8edf2"
                border.width: 1
                border.color: "#101419"

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -9
                    enabled: root.enabled
                    cursorShape: Qt.SizeFDiagCursor
                    onPressed: function(mouse) {
                        const point = parent.mapToItem(root, mouse.x, mouse.y)
                        dragState.startScale = root.boundedScale
                        dragState.startDistance = root.distanceForPoint(point)
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        const point = parent.mapToItem(root, mouse.x, mouse.y)
                        root.emitTransform(root.layerX,
                                           root.layerY,
                                           dragState.startScale * root.distanceForPoint(point) / dragState.startDistance,
                                           root.layerRotation)
                    }
                }
            }
        }

        Rectangle {
            x: 8
            y: 8
            radius: 4
            color: "#a6101419"
            border.width: 1
            border.color: "#3cf4f7fa"
            implicitWidth: transformLabel.implicitWidth + 14
            implicitHeight: transformLabel.implicitHeight + 8

            Text {
                id: transformLabel
                objectName: "photoLayerTransformLabel"
                anchors.centerIn: parent
                text: "X " + Math.round(root.layerX) + "  Y " + Math.round(root.layerY) +
                      "  " + root.layerScale.toFixed(2) + "x  " + root.layerRotation.toFixed(1) + " deg"
                color: "#f4f7fa"
                font.family: "Avenir Next"
                font.pixelSize: 10
                font.weight: Font.Medium
            }
        }
    }
}
