import QtQuick

Item {
    id: root

    property bool active: false
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    readonly property bool guideVisible: active && hasGuide && lineLength > 3
    readonly property real lineLength: Math.sqrt(Math.pow(endScreenX - startScreenX, 2) + Math.pow(endScreenY - startScreenY, 2))
    signal horizonEdited(real startX, real startY, real endX, real endY)

    visible: active && imageWidth > 1 && imageHeight > 1
    enabled: visible

    property bool hasGuide: false
    property real startScreenX: 0
    property real startScreenY: 0
    property real endScreenX: 0
    property real endScreenY: 0

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function pointInImage(mouseX, mouseY) {
        return mouseX >= imageX && mouseX <= imageX + imageWidth &&
               mouseY >= imageY && mouseY <= imageY + imageHeight
    }

    function normalizeScreenPoint(mouseX, mouseY) {
        return {
            x: clamp((mouseX - imageX) / Math.max(1, imageWidth), 0, 1),
            y: clamp((mouseY - imageY) / Math.max(1, imageHeight), 0, 1)
        }
    }

    function setGuideFromScreen(startX, startY, endX, endY) {
        startScreenX = clamp(startX, imageX, imageX + imageWidth)
        startScreenY = clamp(startY, imageY, imageY + imageHeight)
        endScreenX = clamp(endX, imageX, imageX + imageWidth)
        endScreenY = clamp(endY, imageY, imageY + imageHeight)
        hasGuide = true
    }

    function applyNormalizedGuide(startX, startY, endX, endY) {
        if (!active)
            return false
        const boundedStartX = clamp(Number(startX), 0, 1)
        const boundedStartY = clamp(Number(startY), 0, 1)
        const boundedEndX = clamp(Number(endX), 0, 1)
        const boundedEndY = clamp(Number(endY), 0, 1)
        if (Math.abs(boundedEndX - boundedStartX) < 0.0001 &&
            Math.abs(boundedEndY - boundedStartY) < 0.0001)
            return false
        if (imageWidth > 1 && imageHeight > 1) {
            setGuideFromScreen(
                imageX + boundedStartX * imageWidth,
                imageY + boundedStartY * imageHeight,
                imageX + boundedEndX * imageWidth,
                imageY + boundedEndY * imageHeight)
        }
        horizonEdited(boundedStartX, boundedStartY, boundedEndX, boundedEndY)
        return true
    }

    Rectangle {
        x: imageX
        y: imageY
        width: imageWidth
        height: imageHeight
        color: "#26000000"
        border.width: 1
        border.color: "#78f4f7fa"
        visible: root.visible
    }

    Rectangle {
        x: startScreenX
        y: startScreenY - 1
        width: root.lineLength
        height: 2
        radius: 1
        color: "#f4f7fa"
        opacity: 0.92
        visible: root.guideVisible
        rotation: Math.atan2(endScreenY - startScreenY, endScreenX - startScreenX) * 180 / Math.PI
        transformOrigin: Item.Left
    }

    Repeater {
        model: [
            Qt.point(startScreenX, startScreenY),
            Qt.point(endScreenX, endScreenY)
        ]

        delegate: Rectangle {
            required property point modelData

            x: modelData.x - 6
            y: modelData.y - 6
            width: 12
            height: 12
            radius: 6
            color: "#f4f7fa"
            border.width: 2
            border.color: "#101419"
            visible: root.guideVisible
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor

        onPressed: function(mouse) {
            if (!root.pointInImage(mouse.x, mouse.y))
                return
            root.setGuideFromScreen(mouse.x, mouse.y, mouse.x, mouse.y)
        }

        onPositionChanged: function(mouse) {
            if (!pressed || !root.hasGuide)
                return
            root.setGuideFromScreen(root.startScreenX, root.startScreenY, mouse.x, mouse.y)
        }

        onReleased: function(mouse) {
            if (!root.hasGuide)
                return
            root.setGuideFromScreen(root.startScreenX, root.startScreenY, mouse.x, mouse.y)
            const startPoint = root.normalizeScreenPoint(root.startScreenX, root.startScreenY)
            const endPoint = root.normalizeScreenPoint(root.endScreenX, root.endScreenY)
            root.applyNormalizedGuide(startPoint.x, startPoint.y, endPoint.x, endPoint.y)
        }
    }
}
