import QtQuick

Item {
    id: root

    property bool active: false
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property real cropX: 0
    property real cropY: 0
    property real cropWidth: 1
    property real cropHeight: 1
    property real aspectRatio: 0
    property string guideMode: "Thirds"

    signal rectEdited(real x, real y, real width, real height)

    visible: active && imageWidth > 1 && imageHeight > 1
    enabled: visible

    readonly property real minNormalizedSize: 0.05
    readonly property color frameColor: "#f4f7fa"
    readonly property color guideColor: "#cfd6de"
    readonly property real handleSize: 12
    readonly property rect normalizedRect: normalizedCropRect()
    readonly property rect cropScreenRect: Qt.rect(
        imageX + normalizedRect.x * imageWidth,
        imageY + normalizedRect.y * imageHeight,
        normalizedRect.width * imageWidth,
        normalizedRect.height * imageHeight)

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    function pointInRect(mouseX, mouseY, rect) {
        return mouseX >= rect.x && mouseX <= rect.x + rect.width &&
               mouseY >= rect.y && mouseY <= rect.y + rect.height;
    }

    function pointInImage(mouseX, mouseY) {
        return pointInRect(mouseX, mouseY, Qt.rect(imageX, imageY, imageWidth, imageHeight));
    }

    function rectNearlyFullImage(rect) {
        return rect.width >= 0.985 && rect.height >= 0.985 &&
               rect.x <= 0.01 && rect.y <= 0.01;
    }

    function minimumWidth() {
        if (aspectRatio > 0)
            return Math.max(minNormalizedSize, minNormalizedSize * aspectRatio);
        return minNormalizedSize;
    }

    function minimumHeight() {
        if (aspectRatio > 0)
            return Math.max(minNormalizedSize, minNormalizedSize / aspectRatio);
        return minNormalizedSize;
    }

    function normalizedCropRect() {
        const width = clamp(cropWidth > 0 ? cropWidth : 1, minimumWidth(), 1);
        const height = clamp(cropHeight > 0 ? cropHeight : 1, minimumHeight(), 1);
        return Qt.rect(
            clamp(cropX, 0, Math.max(0, 1 - width)),
            clamp(cropY, 0, Math.max(0, 1 - height)),
            width,
            height);
    }

    function emitRect(rect) {
        rectEdited(rect.x, rect.y, rect.width, rect.height);
    }

    function normalizeForImage(mouseX, mouseY) {
        return {
            x: clamp((mouseX - imageX) / Math.max(1, imageWidth), 0, 1),
            y: clamp((mouseY - imageY) / Math.max(1, imageHeight), 0, 1)
        };
    }

    function handleRect(mode) {
        if (mode === "topLeft") {
            return Qt.rect(cropScreenRect.x - handleSize / 2, cropScreenRect.y - handleSize / 2, handleSize, handleSize);
        }
        if (mode === "topRight") {
            return Qt.rect(cropScreenRect.x + cropScreenRect.width - handleSize / 2, cropScreenRect.y - handleSize / 2, handleSize, handleSize);
        }
        if (mode === "bottomLeft") {
            return Qt.rect(cropScreenRect.x - handleSize / 2, cropScreenRect.y + cropScreenRect.height - handleSize / 2, handleSize, handleSize);
        }
        return Qt.rect(cropScreenRect.x + cropScreenRect.width - handleSize / 2, cropScreenRect.y + cropScreenRect.height - handleSize / 2, handleSize, handleSize);
    }

    function hitMode(mouseX, mouseY) {
        if (!pointInImage(mouseX, mouseY))
            return "";
        if (pointInRect(mouseX, mouseY, handleRect("topLeft")))
            return "topLeft";
        if (pointInRect(mouseX, mouseY, handleRect("topRight")))
            return "topRight";
        if (pointInRect(mouseX, mouseY, handleRect("bottomLeft")))
            return "bottomLeft";
        if (pointInRect(mouseX, mouseY, handleRect("bottomRight")))
            return "bottomRight";
        if (pointInRect(mouseX, mouseY, cropScreenRect))
            return rectNearlyFullImage(normalizedRect) ? "draw" : "move";
        return "draw";
    }

    function cursorForMode(mode) {
        if (mode === "topLeft" || mode === "bottomRight")
            return Qt.SizeFDiagCursor;
        if (mode === "topRight" || mode === "bottomLeft")
            return Qt.SizeBDiagCursor;
        if (mode === "move")
            return Qt.SizeAllCursor;
        return Qt.CrossCursor;
    }

    function dragMove(mouseX, mouseY) {
        const deltaX = (mouseX - dragState.startMouseX) / imageWidth;
        const deltaY = (mouseY - dragState.startMouseY) / imageHeight;
        const startRect = dragState.startRect;
        emitRect(Qt.rect(
            clamp(startRect.x + deltaX, 0, Math.max(0, 1 - startRect.width)),
            clamp(startRect.y + deltaY, 0, Math.max(0, 1 - startRect.height)),
            startRect.width,
            startRect.height));
    }

    function dragCorner(mouseX, mouseY, anchorX, anchorY, directionX, directionY) {
        const normalizedMouse = normalizeForImage(mouseX, mouseY);
        const minWidth = minimumWidth();
        const minHeight = minimumHeight();

        let width = directionX > 0 ? normalizedMouse.x - anchorX : anchorX - normalizedMouse.x;
        let height = directionY > 0 ? normalizedMouse.y - anchorY : anchorY - normalizedMouse.y;
        width = Math.max(minWidth, width);
        height = Math.max(minHeight, height);

        const maxWidth = directionX > 0 ? 1 - anchorX : anchorX;
        const maxHeight = directionY > 0 ? 1 - anchorY : anchorY;

        if (aspectRatio > 0) {
            if (width / Math.max(height, 1e-6) > aspectRatio) {
                height = width / aspectRatio;
            } else {
                width = height * aspectRatio;
            }

            if (width > maxWidth) {
                width = maxWidth;
                height = width / aspectRatio;
            }
            if (height > maxHeight) {
                height = maxHeight;
                width = height * aspectRatio;
            }

            width = Math.max(minWidth, width);
            height = Math.max(minHeight, height);
        } else {
            width = clamp(width, minWidth, maxWidth);
            height = clamp(height, minHeight, maxHeight);
        }

        const nextX = directionX > 0 ? anchorX : anchorX - width;
        const nextY = directionY > 0 ? anchorY : anchorY - height;
        emitRect(Qt.rect(
            clamp(nextX, 0, Math.max(0, 1 - width)),
            clamp(nextY, 0, Math.max(0, 1 - height)),
            width,
            height));
    }

    function dragDraw(mouseX, mouseY) {
        const startX = dragState.startNormalizedX;
        const startY = dragState.startNormalizedY;
        const current = normalizeForImage(mouseX, mouseY);
        const directionX = current.x >= startX ? 1 : -1;
        const directionY = current.y >= startY ? 1 : -1;
        const minWidth = minimumWidth();
        const minHeight = minimumHeight();
        const maxWidth = directionX > 0 ? 1 - startX : startX;
        const maxHeight = directionY > 0 ? 1 - startY : startY;

        let width = Math.max(minWidth, Math.abs(current.x - startX));
        let height = Math.max(minHeight, Math.abs(current.y - startY));

        if (aspectRatio > 0) {
            if (width / Math.max(height, 1e-6) > aspectRatio) {
                height = width / aspectRatio;
            } else {
                width = height * aspectRatio;
            }

            if (width > maxWidth) {
                width = maxWidth;
                height = width / aspectRatio;
            }
            if (height > maxHeight) {
                height = maxHeight;
                width = height * aspectRatio;
            }

            width = Math.max(minWidth, width);
            height = Math.max(minHeight, height);
        } else {
            width = clamp(width, minWidth, maxWidth);
            height = clamp(height, minHeight, maxHeight);
        }

        const rectX = directionX > 0 ? startX : startX - width;
        const rectY = directionY > 0 ? startY : startY - height;
        emitRect(Qt.rect(
            clamp(rectX, 0, Math.max(0, 1 - width)),
            clamp(rectY, 0, Math.max(0, 1 - height)),
            width,
            height));
    }

    function updateDrag(mouseX, mouseY) {
        const startRect = dragState.startRect;
        if (dragState.mode === "move") {
            dragMove(mouseX, mouseY);
            return;
        }
        if (dragState.mode === "draw") {
            dragDraw(mouseX, mouseY);
            return;
        }
        if (dragState.mode === "topLeft") {
            dragCorner(mouseX, mouseY, startRect.x + startRect.width, startRect.y + startRect.height, -1, -1);
        } else if (dragState.mode === "topRight") {
            dragCorner(mouseX, mouseY, startRect.x, startRect.y + startRect.height, 1, -1);
        } else if (dragState.mode === "bottomLeft") {
            dragCorner(mouseX, mouseY, startRect.x + startRect.width, startRect.y, -1, 1);
        } else if (dragState.mode === "bottomRight") {
            dragCorner(mouseX, mouseY, startRect.x, startRect.y, 1, 1);
        }
    }

    QtObject {
        id: dragState

        property string mode: ""
        property real startMouseX: 0
        property real startMouseY: 0
        property real startNormalizedX: 0
        property real startNormalizedY: 0
        property rect startRect: Qt.rect(0, 0, 1, 1)
    }

    Rectangle {
        x: imageX
        y: imageY
        width: imageWidth
        height: Math.max(0, cropScreenRect.y - imageY)
        color: "#8a091117"
        visible: root.visible
    }

    Rectangle {
        x: imageX
        y: cropScreenRect.y
        width: Math.max(0, cropScreenRect.x - imageX)
        height: cropScreenRect.height
        color: "#8a091117"
        visible: root.visible
    }

    Rectangle {
        x: cropScreenRect.x + cropScreenRect.width
        y: cropScreenRect.y
        width: Math.max(0, imageX + imageWidth - x)
        height: cropScreenRect.height
        color: "#8a091117"
        visible: root.visible
    }

    Rectangle {
        x: imageX
        y: cropScreenRect.y + cropScreenRect.height
        width: imageWidth
        height: Math.max(0, imageY + imageHeight - y)
        color: "#8a091117"
        visible: root.visible
    }

    Item {
        id: cropFrame
        x: cropScreenRect.x
        y: cropScreenRect.y
        width: cropScreenRect.width
        height: cropScreenRect.height
        visible: root.visible

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 1
            border.color: frameColor
        }

        Rectangle {
            width: 1
            height: parent.height
            x: parent.width / 3
            color: guideColor
            opacity: guideMode === "Thirds" ? 0.7 : 0
        }

        Rectangle {
            width: 1
            height: parent.height
            x: parent.width * 2 / 3
            color: guideColor
            opacity: guideMode === "Thirds" ? 0.7 : 0
        }

        Rectangle {
            width: parent.width
            height: 1
            y: parent.height / 3
            color: guideColor
            opacity: guideMode === "Thirds" ? 0.7 : 0
        }

        Rectangle {
            width: parent.width
            height: 1
            y: parent.height * 2 / 3
            color: guideColor
            opacity: guideMode === "Thirds" ? 0.7 : 0
        }

        Rectangle {
            width: 1
            height: parent.height
            x: parent.width / 2
            color: guideColor
            opacity: guideMode === "Center" ? 0.7 : 0
        }

        Rectangle {
            width: parent.width
            height: 1
            y: parent.height / 2
            color: guideColor
            opacity: guideMode === "Center" ? 0.7 : 0
        }

        Rectangle {
            width: Math.sqrt(parent.width * parent.width + parent.height * parent.height)
            height: 1
            x: 0
            y: 0
            color: guideColor
            opacity: guideMode === "Diagonal" ? 0.7 : 0
            rotation: Math.atan2(parent.height, parent.width) * 180 / Math.PI
            transformOrigin: Item.Left
        }

        Rectangle {
            width: Math.sqrt(parent.width * parent.width + parent.height * parent.height)
            height: 1
            x: 0
            y: parent.height
            color: guideColor
            opacity: guideMode === "Diagonal" ? 0.7 : 0
            rotation: -Math.atan2(parent.height, parent.width) * 180 / Math.PI
            transformOrigin: Item.Left
        }
    }

    Repeater {
        model: [
            handleRect("topLeft"),
            handleRect("topRight"),
            handleRect("bottomLeft"),
            handleRect("bottomRight")
        ]

        delegate: Rectangle {
            required property var modelData

            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            radius: 2
            color: "#e8edf2"
            border.width: 1
            border.color: "#101419"
            visible: root.visible
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: cursorForMode(dragState.mode !== "" ? dragState.mode : hitMode(mouseX, mouseY))

        onPressed: function(mouse) {
            if (!pointInImage(mouse.x, mouse.y))
                return;

            dragState.mode = hitMode(mouse.x, mouse.y);
            dragState.startMouseX = mouse.x;
            dragState.startMouseY = mouse.y;
            dragState.startRect = normalizedRect;

            const normalized = normalizeForImage(mouse.x, mouse.y);
            dragState.startNormalizedX = normalized.x;
            dragState.startNormalizedY = normalized.y;

            if (dragState.mode === "draw") {
                emitRect(Qt.rect(normalized.x, normalized.y, minimumWidth(), minimumHeight()));
            }
        }

        onPositionChanged: function(mouse) {
            if (pressed && dragState.mode !== "") {
                updateDrag(mouse.x, mouse.y);
            }
        }

        onReleased: dragState.mode = ""
        onCanceled: dragState.mode = ""
    }
}
