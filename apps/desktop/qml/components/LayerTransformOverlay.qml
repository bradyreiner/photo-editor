pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    Theme { id: theme }

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
    property bool textBoxResize: false
    property bool moveSelection: false
    property bool snappingEnabled: true
    property var snapTargetsX: [{ position: 0, label: "Left edge" }, { position: canvasWidth / 2, label: "Center" }, { position: canvasWidth, label: "Right edge" }]
    property var snapTargetsY: [{ position: 0, label: "Top edge" }, { position: canvasHeight / 2, label: "Center" }, { position: canvasHeight, label: "Bottom edge" }]
    property rect snapGuideBounds: Qt.rect(imageX, imageY, imageWidth, imageHeight)
    property real snapGuideX: Number.NaN
    property real snapGuideY: Number.NaN
    property string snapGuideLabel: ""
    readonly property real snapDistance: theme.scaled(6)

    signal transformEdited(int x, int y, real scale, real rotation)
    signal positionEdited(int x, int y)
    signal textBoxEdited(int x, int y, int width, int height)
    signal editRequested()
    signal layerPickRequested(real viewportX, real viewportY, int modifiers)
    signal interactionStarted()
    signal interactionFinished()

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
    readonly property color guideColor: locked ? theme.textMuted : theme.primaryAccent
    property bool interactionActive: false
    property bool handleHovered: false

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function emitTransform(nextX, nextY, nextScale, nextRotation) {
        transformEdited(Math.round(clamp(nextX, -1000000, 1000000)),
                        Math.round(clamp(nextY, -1000000, 1000000)),
                        clamp(nextScale, 0.05, 32),
                        clamp(nextRotation, -3600, 3600))
    }

    function angleForPoint(point, originX, originY) {
        return Math.atan2(point.y - originY, point.x - originX) * 180 / Math.PI
    }

    function distanceForPoint(point) {
        const dx = point.x - centerX
        const dy = point.y - centerY
        return Math.max(1, Math.sqrt(dx * dx + dy * dy))
    }

    function pointerInRoot(mouse, sourceItem) {
        return sourceItem.mapToItem(root, mouse.x, mouse.y)
    }

    function wrappedAngleDelta(delta) {
        return ((delta + 180) % 360 + 360) % 360 - 180
    }

    function beginInteraction() {
        interactionActive = true
        interactionStarted()
    }

    function endInteraction() {
        if (!interactionActive)
            return
        interactionActive = false
        snapGuideX = Number.NaN
        snapGuideY = Number.NaN
        snapGuideLabel = ""
        interactionFinished()
    }

    function nearestSnap(anchors, targets, viewportScale) {
        let best = null
        let distance = snapDistance / Math.max(0.00001, viewportScale)
        for (let target of targets) {
            for (let anchor of anchors) {
                const delta = Number(target.position) - anchor
                if (Math.abs(delta) < distance) {
                    distance = Math.abs(delta)
                    best = { delta: delta, position: Number(target.position), label: String(target.label || "Guide") }
                }
            }
        }
        return best
    }

    function moveFromDrag(point, modifiers) {
        let dx = point.x - dragState.startX
        let dy = point.y - dragState.startY
        if (modifiers & Qt.ShiftModifier) {
            if (Math.abs(dx) >= Math.abs(dy)) dy = 0
            else dx = 0
        }
        let nextX = dragState.startLayerX + dx / viewportScaleX
        let nextY = dragState.startLayerY + dy / viewportScaleY
        snapGuideX = Number.NaN
        snapGuideY = Number.NaN
        snapGuideLabel = ""
        if (snappingEnabled && dragState.moved && !(modifiers & Qt.AltModifier)) {
            const radians = layerRotation * Math.PI / 180
            const width = layerWidth * boundedScale
            const height = layerHeight * boundedScale
            const extentX = Math.abs(Math.cos(radians)) * width + Math.abs(Math.sin(radians)) * height
            const extentY = Math.abs(Math.sin(radians)) * width + Math.abs(Math.cos(radians)) * height
            const cx = nextX + width / 2
            const cy = nextY + height / 2
            const horizontalOnly = (modifiers & Qt.ShiftModifier) && Math.abs(dx) >= Math.abs(dy)
            const verticalOnly = (modifiers & Qt.ShiftModifier) && !horizontalOnly
            const xSnap = verticalOnly ? null : nearestSnap([cx, cx - extentX / 2, cx + extentX / 2], snapTargetsX, viewportScaleX)
            const ySnap = horizontalOnly ? null : nearestSnap([cy, cy - extentY / 2, cy + extentY / 2], snapTargetsY, viewportScaleY)
            if (xSnap) {
                nextX += xSnap.delta
                snapGuideX = xSnap.position
                snapGuideLabel = xSnap.label
            }
            if (ySnap) {
                nextY += ySnap.delta
                snapGuideY = ySnap.position
                if (!snapGuideLabel.length) snapGuideLabel = ySnap.label
            }
        }
        emitPosition(nextX, nextY)
    }

    function resizeFromDrag(point, centerResize) {
        if (!enabled)
            return false
        const pointerDx = point.x - dragState.pointerX
        const pointerDy = point.y - dragState.pointerY
        let ratio = 1
        if (centerResize) {
            const vx = dragState.cornerX - dragState.centerX + pointerDx
            const vy = dragState.cornerY - dragState.centerY + pointerDy
            ratio = (vx * dragState.centerVectorX + vy * dragState.centerVectorY) /
                    Math.max(0.00000001, dragState.centerVectorX * dragState.centerVectorX +
                             dragState.centerVectorY * dragState.centerVectorY)
        } else {
            const vx = dragState.cornerX - dragState.anchorX + pointerDx
            const vy = dragState.cornerY - dragState.anchorY + pointerDy
            ratio = (vx * dragState.diagonalX + vy * dragState.diagonalY) /
                    Math.max(0.00000001, dragState.diagonalX * dragState.diagonalX +
                             dragState.diagonalY * dragState.diagonalY)
        }
        const nextScale = clamp(dragState.startScale * ratio, 0.05, 32)
        const appliedRatio = nextScale / dragState.startScale
        const nextWidth = dragState.startWidth * appliedRatio
        const nextHeight = dragState.startHeight * appliedRatio
        const radians = dragState.startRotation * Math.PI / 180
        const cosRotation = Math.cos(radians)
        const sinRotation = Math.sin(radians)
        let nextFrameX
        let nextFrameY
        if (centerResize) {
            nextFrameX = dragState.centerX - nextWidth / 2
            nextFrameY = dragState.centerY - nextHeight / 2
        } else {
            const oppositeX = (dragState.oppositeCornerX - 0.5) * nextWidth
            const oppositeY = (dragState.oppositeCornerY - 0.5) * nextHeight
            nextFrameX = dragState.anchorX - nextWidth / 2 - (cosRotation * oppositeX - sinRotation * oppositeY)
            nextFrameY = dragState.anchorY - nextHeight / 2 - (sinRotation * oppositeX + cosRotation * oppositeY)
        }
        emitTransform((nextFrameX - imageX) / viewportScaleX,
                      (nextFrameY - imageY) / viewportScaleY,
                      nextScale,
                      dragState.startRotation)
        return true
    }

    function resizeTextBoxFromDrag(point, handle, centerResize, keepAspect) {
        if (!enabled || !textBoxResize)
            return false

        const pointerDx = point.x - dragState.pointerX
        const pointerDy = point.y - dragState.pointerY
        const radians = dragState.startRotation * Math.PI / 180
        const cosRotation = Math.cos(radians)
        const sinRotation = Math.sin(radians)
        const localDeltaX = pointerDx * cosRotation + pointerDy * sinRotation
        const localDeltaY = -pointerDx * sinRotation + pointerDy * cosRotation
        const signX = handle.x < 0.5 ? -1 : handle.x > 0.5 ? 1 : 0
        const signY = handle.y < 0.5 ? -1 : handle.y > 0.5 ? 1 : 0
        const centeredFactor = centerResize ? 2 : 1
        let nextWidth = dragState.startLayerWidth
        let nextHeight = dragState.startLayerHeight

        if (keepAspect) {
            let ratio = 1
            if (signX !== 0 && signY !== 0) {
                const baseX = centerResize ? dragState.centerVectorX : dragState.diagonalX
                const baseY = centerResize ? dragState.centerVectorY : dragState.diagonalY
                const movedX = baseX + pointerDx
                const movedY = baseY + pointerDy
                ratio = (movedX * baseX + movedY * baseY) /
                    Math.max(0.00000001, baseX * baseX + baseY * baseY)
            } else if (signX !== 0) {
                const baseWidth = dragState.startLayerWidth * dragState.startScale * viewportScaleX
                ratio = 1 + signX * centeredFactor * localDeltaX / Math.max(0.00000001, baseWidth)
            } else if (signY !== 0) {
                const baseHeight = dragState.startLayerHeight * dragState.startScale * viewportScaleY
                ratio = 1 + signY * centeredFactor * localDeltaY / Math.max(0.00000001, baseHeight)
            }
            nextWidth *= ratio
            nextHeight *= ratio
        } else {
            if (signX !== 0)
                nextWidth += centeredFactor * signX * localDeltaX /
                    Math.max(0.00000001, dragState.startScale * viewportScaleX)
            if (signY !== 0)
                nextHeight += centeredFactor * signY * localDeltaY /
                    Math.max(0.00000001, dragState.startScale * viewportScaleY)
        }

        nextWidth = clamp(nextWidth, 16, 8192)
        nextHeight = clamp(nextHeight, 16, 8192)
        if (Math.abs(nextWidth - dragState.startLayerWidth) < 0.01 &&
                Math.abs(nextHeight - dragState.startLayerHeight) < 0.01)
            return false

        const nextFrameWidth = nextWidth * dragState.startScale * viewportScaleX
        const nextFrameHeight = nextHeight * dragState.startScale * viewportScaleY
        let nextFrameX
        let nextFrameY
        if (centerResize) {
            nextFrameX = dragState.centerX - nextFrameWidth / 2
            nextFrameY = dragState.centerY - nextFrameHeight / 2
        } else {
            const oppositeX = (dragState.oppositeCornerX - 0.5) * nextFrameWidth
            const oppositeY = (dragState.oppositeCornerY - 0.5) * nextFrameHeight
            nextFrameX = dragState.anchorX - nextFrameWidth / 2 -
                (cosRotation * oppositeX - sinRotation * oppositeY)
            nextFrameY = dragState.anchorY - nextFrameHeight / 2 -
                (sinRotation * oppositeX + cosRotation * oppositeY)
        }
        textBoxEdited(Math.round(clamp((nextFrameX - imageX) / viewportScaleX, -1000000, 1000000)),
                      Math.round(clamp((nextFrameY - imageY) / viewportScaleY, -1000000, 1000000)),
                      Math.round(nextWidth), Math.round(nextHeight))
        return true
    }

    function resizeCursorShape(cornerX, cornerY) {
        const deltaX = (cornerX === 0 ? -1 : 1) * frameWidth
        const deltaY = (cornerY === 0 ? -1 : 1) * frameHeight
        let axis = Math.atan2(deltaY, deltaX) * 180 / Math.PI + layerRotation
        axis = ((axis % 180) + 180) % 180
        const forwardDistance = Math.abs(axis - 45)
        const backwardDistance = Math.abs(axis - 135)
        return forwardDistance <= backwardDistance ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
    }

    function textBoxResizeCursorShape(handleX, handleY) {
        if (handleX !== 0.5 && handleY !== 0.5)
            return resizeCursorShape(handleX, handleY)
        let axis = layerRotation + (handleX === 0.5 ? 90 : 0)
        axis = ((axis % 180) + 180) % 180
        const horizontalDistance = Math.min(axis, 180 - axis)
        return horizontalDistance <= 45 ? Qt.SizeHorCursor : Qt.SizeVerCursor
    }

    function resizeHandleHitMargin(handleSize, frameExtent) {
        return Math.max(-12, (handleSize - Math.max(1, frameExtent / 2 - 1)) / 2)
    }

    function moveByViewportDelta(deltaX, deltaY) {
        if (!enabled)
            return false
        emitPosition(layerX + deltaX / viewportScaleX, layerY + deltaY / viewportScaleY)
        return true
    }

    function emitPosition(x, y) {
        if (moveSelection)
            positionEdited(Math.round(clamp(x, -1000000, 1000000)),
                           Math.round(clamp(y, -1000000, 1000000)))
        else
            emitTransform(x, y, layerScale, layerRotation)
    }

    function scaleFromViewportPoint(pointX, pointY) {
        if (!enabled || textBoxResize)
            return false
        const startDistance = Math.max(1, Math.sqrt(frameWidth * frameWidth + frameHeight * frameHeight) / 2)
        emitTransform(layerX, layerY,
                      boundedScale * distanceForPoint(Qt.point(pointX, pointY)) / startDistance,
                      layerRotation)
        return true
    }

    function rotateFromViewportPoint(pointX, pointY) {
        if (!enabled)
            return false
        emitTransform(layerX, layerY,
                      layerScale,
                      angleForPoint(Qt.point(pointX, pointY), centerX, centerY) + 90)
        return true
    }

    QtObject {
        id: dragState

        property real startX: 0
        property real startY: 0
        property bool moved: false
        property real startLayerX: 0
        property real startLayerY: 0
        property real startScale: 1
        property real startDistance: 1
        property real startRotation: 0
        property real startAngle: 0
        property real lastAngle: 0
        property real angleDelta: 0
        property real startWidth: 1
        property real startHeight: 1
        property real startLayerWidth: 1
        property real startLayerHeight: 1
        property real pointerX: 0
        property real pointerY: 0
        property real cornerX: 0
        property real cornerY: 0
        property real oppositeCornerX: 0
        property real oppositeCornerY: 0
        property real anchorX: 0
        property real anchorY: 0
        property real centerX: 0
        property real centerY: 0
        property real diagonalX: 1
        property real diagonalY: 1
        property real centerVectorX: 1
        property real centerVectorY: 1
    }

    Rectangle {
        objectName: "photoLayerSnapGuideX"
        visible: root.interactionActive && Number.isFinite(root.snapGuideX)
        x: root.imageX + root.snapGuideX * root.viewportScaleX
        y: root.snapGuideBounds.y
        width: theme.hairlineWidth
        height: root.snapGuideBounds.height
        color: theme.primaryAccent
        opacity: 0.7
    }
    Rectangle {
        objectName: "photoLayerSnapGuideY"
        visible: root.interactionActive && Number.isFinite(root.snapGuideY)
        x: root.snapGuideBounds.x
        y: root.imageY + root.snapGuideY * root.viewportScaleY
        width: root.snapGuideBounds.width
        height: theme.hairlineWidth
        color: theme.primaryAccent
        opacity: 0.7
    }
    Text {
        objectName: "photoLayerSnapGuideLabel"
        visible: root.interactionActive && root.snapGuideLabel.length > 0
        x: Math.max(8, Math.min(root.width - width - 8,
            Number.isFinite(root.snapGuideX) ? root.imageX + root.snapGuideX * root.viewportScaleX + 8 : root.frameX))
        y: Math.max(8, root.snapGuideBounds.y + 8)
        text: root.snapGuideLabel
        color: theme.primaryAccent
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
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
            border.width: theme.hairlineWidth
            border.color: root.guideColor
            opacity: root.locked ? 0.48 : 0.88
        }

        MouseArea {
            id: moveArea
            objectName: "photoLayerTransformMoveHandle"
            anchors.fill: parent
            enabled: root.enabled
            hoverEnabled: true
            cursorShape: Qt.SizeAllCursor
            onPressed: function(mouse) {
                root.forceActiveFocus()
                root.beginInteraction()
                const point = root.pointerInRoot(mouse, frame)
                dragState.startX = point.x
                dragState.startY = point.y
                dragState.moved = false
                dragState.startLayerX = root.layerX
                dragState.startLayerY = root.layerY
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                const point = root.pointerInRoot(mouse, frame)
                const deltaX = point.x - dragState.startX
                const deltaY = point.y - dragState.startY
                // QML metadata types Qt.styleHints as QObject despite this Qt property.
                // qmllint disable missing-property
                if (Math.sqrt(deltaX * deltaX + deltaY * deltaY) >= Qt.styleHints.startDragDistance)
                    dragState.moved = true
                // qmllint enable missing-property
                root.moveFromDrag(point, mouse.modifiers)
            }
            onDoubleClicked: if (!dragState.moved) root.editRequested()
            onClicked: function(mouse) {
                if (dragState.moved)
                    return
                const point = root.pointerInRoot(mouse, frame)
                root.layerPickRequested(point.x, point.y, mouse.modifiers)
            }
            onReleased: root.endInteraction()
            onCanceled: root.endInteraction()
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: -28
            width: 1
            height: 22
            color: root.guideColor
            opacity: 0.76
        }

        Rectangle {
            objectName: "photoLayerTransformRotateHandle"
            anchors.horizontalCenter: parent.horizontalCenter
            y: -36
            width: 10
            height: 10
            radius: 5
            color: root.locked ? theme.textMuted : rotateArea.pressed ? theme.textStrong
                   : rotateArea.containsMouse ? theme.opticalLight : theme.textStrong
            border.width: 1
            border.color: "#101419"

            MouseArea {
                id: rotateArea
                anchors.fill: parent
                anchors.margins: -12
                enabled: root.enabled
                hoverEnabled: true
                cursorShape: Qt.CrossCursor
                onEntered: root.handleHovered = true
                onExited: root.handleHovered = false
                onPressed: function(mouse) {
                    root.beginInteraction()
                    const point = root.pointerInRoot(mouse, rotateArea)
                    dragState.startRotation = root.layerRotation
                    dragState.startAngle = root.angleForPoint(point, root.centerX, root.centerY)
                    dragState.lastAngle = dragState.startAngle
                    dragState.angleDelta = 0
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    const point = root.pointerInRoot(mouse, rotateArea)
                    const angle = root.angleForPoint(point, root.centerX, root.centerY)
                    dragState.angleDelta += root.wrappedAngleDelta(angle - dragState.lastAngle)
                    dragState.lastAngle = angle
                    let nextRotation = dragState.startRotation + dragState.angleDelta
                    if (mouse.modifiers & Qt.ShiftModifier)
                        nextRotation = Math.round(nextRotation / 15) * 15
                    root.emitTransform(root.layerX,
                                       root.layerY,
                                       root.layerScale,
                                       nextRotation)
                }
                onReleased: root.endInteraction()
                onCanceled: root.endInteraction()
            }
        }

        Repeater {
            model: root.textBoxResize ? [
                { name: "photoLayerTransformTopLeftHandle", x: 0, y: 0 },
                { name: "photoLayerTransformTopRightHandle", x: 1, y: 0 },
                { name: "photoLayerTransformBottomLeftHandle", x: 0, y: 1 },
                { name: "photoLayerTransformBottomRightHandle", x: 1, y: 1 },
                { name: "photoLayerTransformTopHandle", x: 0.5, y: 0 },
                { name: "photoLayerTransformRightHandle", x: 1, y: 0.5 },
                { name: "photoLayerTransformBottomHandle", x: 0.5, y: 1 },
                { name: "photoLayerTransformLeftHandle", x: 0, y: 0.5 }
            ] : [
                { name: "photoLayerTransformTopLeftHandle", x: 0, y: 0 },
                { name: "photoLayerTransformTopRightHandle", x: 1, y: 0 },
                { name: "photoLayerTransformBottomLeftHandle", x: 0, y: 1 },
                { name: "photoLayerTransformBottomRightHandle", x: 1, y: 1 }
            ]

            Rectangle {
                id: handle
                required property var modelData

                objectName: modelData.name
                z: modelData.x !== 0.5 && modelData.y !== 0.5 ? 1 : 0
                x: modelData.x * parent.width - width / 2
                y: modelData.y * parent.height - height / 2
                width: 10
                height: 10
                radius: 1
                color: root.locked ? theme.textMuted : resizeArea.pressed ? theme.textStrong
                       : resizeArea.containsMouse ? theme.opticalLight : theme.textStrong
                border.width: 1
                border.color: "#101419"

                MouseArea {
                    id: resizeArea
                    anchors.fill: parent
                    anchors.leftMargin: root.resizeHandleHitMargin(handle.width, frame.width)
                    anchors.rightMargin: root.resizeHandleHitMargin(handle.width, frame.width)
                    anchors.topMargin: root.resizeHandleHitMargin(handle.height, frame.height)
                    anchors.bottomMargin: root.resizeHandleHitMargin(handle.height, frame.height)
                    enabled: root.enabled
                    hoverEnabled: true
                    cursorShape: root.textBoxResize
                        ? root.textBoxResizeCursorShape(handle.modelData.x, handle.modelData.y)
                        : root.resizeCursorShape(handle.modelData.x, handle.modelData.y)
                    onEntered: root.handleHovered = true
                    onExited: root.handleHovered = false
                    onPressed: function(mouse) {
                        root.beginInteraction()
                        const point = root.pointerInRoot(mouse, resizeArea)
                        dragState.startScale = root.boundedScale
                        dragState.startRotation = root.layerRotation
                        dragState.startWidth = root.frameWidth
                        dragState.startHeight = root.frameHeight
                        dragState.startLayerWidth = root.layerWidth
                        dragState.startLayerHeight = root.layerHeight
                        dragState.pointerX = point.x
                        dragState.pointerY = point.y
                        dragState.cornerX = frame.mapToItem(root, handle.modelData.x * frame.width, handle.modelData.y * frame.height).x
                        dragState.cornerY = frame.mapToItem(root, handle.modelData.x * frame.width, handle.modelData.y * frame.height).y
                        dragState.oppositeCornerX = 1 - handle.modelData.x
                        dragState.oppositeCornerY = 1 - handle.modelData.y
                        const anchor = frame.mapToItem(root,
                                                       dragState.oppositeCornerX * frame.width,
                                                       dragState.oppositeCornerY * frame.height)
                        dragState.anchorX = anchor.x
                        dragState.anchorY = anchor.y
                        const center = frame.mapToItem(root, frame.width / 2, frame.height / 2)
                        dragState.centerX = center.x
                        dragState.centerY = center.y
                        dragState.diagonalX = dragState.cornerX - dragState.anchorX
                        dragState.diagonalY = dragState.cornerY - dragState.anchorY
                        dragState.centerVectorX = dragState.cornerX - dragState.centerX
                        dragState.centerVectorY = dragState.cornerY - dragState.centerY
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        const point = root.pointerInRoot(mouse, resizeArea)
                        if (root.textBoxResize) {
                            root.resizeTextBoxFromDrag(point,
                                                       handle.modelData,
                                                       Boolean(mouse.modifiers & Qt.AltModifier),
                                                       Boolean(mouse.modifiers & Qt.ShiftModifier))
                        } else {
                            root.resizeFromDrag(point, Boolean(mouse.modifiers & Qt.AltModifier))
                        }
                    }
                    onReleased: root.endInteraction()
                    onCanceled: root.endInteraction()
                }
            }
        }

        Rectangle {
            parent: root
            x: root.imageX + theme.space8
            y: root.imageY + root.imageHeight - implicitHeight - theme.space8
            radius: theme.radiusControl
            color: "#a6101419"
            border.width: 1
            border.color: "#3cf4f7fa"
            visible: root.interactionActive || moveArea.containsMouse || rotateArea.containsMouse || root.handleHovered
            implicitWidth: transformLabel.implicitWidth + 14
            implicitHeight: transformLabel.implicitHeight + 8

            Text {
                id: transformLabel
                objectName: "photoLayerTransformLabel"
                anchors.centerIn: parent
                text: root.textBoxResize
                    ? "W " + Math.round(root.layerWidth) + "  H " + Math.round(root.layerHeight) + " px  " +
                      root.layerRotation.toFixed(1) + " deg"
                    : "X " + Math.round(root.layerX) + "  Y " + Math.round(root.layerY) +
                      "  " + root.layerScale.toFixed(2) + "x  " + root.layerRotation.toFixed(1) + " deg"
                color: "#f4f7fa"
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                font.weight: Font.Medium
            }
        }
    }
}
