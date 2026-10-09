import QtQuick

Item {
    id: root

    readonly property int maximumCanvasBackingEdge: 2048
    readonly property string canvasMemorySafetyContract: "crop-canvas-memory:v1|max-backing-edge:2048|hidden-or-oversized:1x1|repaint:visible-safe-only"

    property bool active: false
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property int sourceWidth: 0
    property int sourceHeight: 0
    property real cropX: 0
    property real cropY: 0
    property real cropWidth: 1
    property real cropHeight: 1
    property real aspectRatio: 0
    property string guideMode: "Thirds"
    property color accentColor: "#37e6bf"

    signal rectEdited(real x, real y, real width, real height)
    signal interactionStarted()
    signal interactionFinished()

    visible: active && imageWidth > 1 && imageHeight > 1
    enabled: visible

    readonly property real minNormalizedSize: 0.05
    readonly property real goldenRatio: 1.61803398875
    readonly property real goldenMajor: 1 / goldenRatio
    readonly property real goldenMinor: 1 - goldenMajor
    readonly property color frameColor: accentColor
    readonly property color guideColor: "#d8e0e7"
    readonly property real guideLineOpacity: 0.38
    readonly property real guideSpiralOpacity: 0.42
    readonly property real handleVisualSize: 14
    readonly property real handleHitSize: 30
    readonly property real edgeHandleLength: 38
    readonly property var visualHandleNames: [
        "topLeft", "topRight", "bottomLeft", "bottomRight",
        "top", "bottom", "left", "right"
    ]
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
        const ratio = constraintAspectRatio();
        if (ratio > 0)
            return Math.max(minNormalizedSize, minNormalizedSize * ratio);
        return minNormalizedSize;
    }

    function minimumHeight() {
        const ratio = constraintAspectRatio();
        if (ratio > 0)
            return Math.max(minNormalizedSize, minNormalizedSize / ratio);
        return minNormalizedSize;
    }

    function constraintAspectRatio() {
        if (aspectRatio <= 0 || imageWidth <= 0 || imageHeight <= 0)
            return 0;
        return aspectRatio * imageHeight / imageWidth;
    }

    function cropPixelSummary() {
        if (sourceWidth <= 0 || sourceHeight <= 0)
            return Math.round(normalizedRect.width * 100) + "% x " +
                   Math.round(normalizedRect.height * 100) + "%";
        const width = Math.max(1, Math.round(sourceWidth * normalizedRect.width));
        const height = Math.max(1, Math.round(sourceHeight * normalizedRect.height));
        return width + " x " + height + " px";
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
        return handleRectForSize(mode, handleHitSize);
    }

    function visualHandleRect(mode) {
        if (mode === "top" || mode === "bottom" || mode === "left" || mode === "right")
            return edgeVisualHandleRect(mode);
        return handleRectForSize(mode, handleVisualSize);
    }

    function edgeHitRect(mode) {
        const inset = handleHitSize * 0.55;
        if (mode === "top")
            return Qt.rect(cropScreenRect.x + inset, cropScreenRect.y - handleHitSize / 2,
                           Math.max(0, cropScreenRect.width - inset * 2), handleHitSize);
        if (mode === "bottom")
            return Qt.rect(cropScreenRect.x + inset,
                           cropScreenRect.y + cropScreenRect.height - handleHitSize / 2,
                           Math.max(0, cropScreenRect.width - inset * 2), handleHitSize);
        if (mode === "left")
            return Qt.rect(cropScreenRect.x - handleHitSize / 2, cropScreenRect.y + inset,
                           handleHitSize, Math.max(0, cropScreenRect.height - inset * 2));
        return Qt.rect(cropScreenRect.x + cropScreenRect.width - handleHitSize / 2,
                       cropScreenRect.y + inset,
                       handleHitSize, Math.max(0, cropScreenRect.height - inset * 2));
    }

    function edgeVisualHandleRect(mode) {
        const thickness = 4;
        if (mode === "top")
            return Qt.rect(cropScreenRect.x + cropScreenRect.width / 2 - edgeHandleLength / 2,
                           cropScreenRect.y - thickness / 2, edgeHandleLength, thickness);
        if (mode === "bottom")
            return Qt.rect(cropScreenRect.x + cropScreenRect.width / 2 - edgeHandleLength / 2,
                           cropScreenRect.y + cropScreenRect.height - thickness / 2,
                           edgeHandleLength, thickness);
        if (mode === "left")
            return Qt.rect(cropScreenRect.x - thickness / 2,
                           cropScreenRect.y + cropScreenRect.height / 2 - edgeHandleLength / 2,
                           thickness, edgeHandleLength);
        return Qt.rect(cropScreenRect.x + cropScreenRect.width - thickness / 2,
                       cropScreenRect.y + cropScreenRect.height / 2 - edgeHandleLength / 2,
                       thickness, edgeHandleLength);
    }

    function handleRectForSize(mode, size) {
        if (mode === "topLeft") {
            return Qt.rect(cropScreenRect.x - size / 2, cropScreenRect.y - size / 2, size, size);
        }
        if (mode === "topRight") {
            return Qt.rect(cropScreenRect.x + cropScreenRect.width - size / 2, cropScreenRect.y - size / 2, size, size);
        }
        if (mode === "bottomLeft") {
            return Qt.rect(cropScreenRect.x - size / 2, cropScreenRect.y + cropScreenRect.height - size / 2, size, size);
        }
        return Qt.rect(cropScreenRect.x + cropScreenRect.width - size / 2, cropScreenRect.y + cropScreenRect.height - size / 2, size, size);
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
        if (pointInRect(mouseX, mouseY, edgeHitRect("top")))
            return "top";
        if (pointInRect(mouseX, mouseY, edgeHitRect("bottom")))
            return "bottom";
        if (pointInRect(mouseX, mouseY, edgeHitRect("left")))
            return "left";
        if (pointInRect(mouseX, mouseY, edgeHitRect("right")))
            return "right";
        if (pointInRect(mouseX, mouseY, cropScreenRect))
            return rectNearlyFullImage(normalizedRect) ? "draw" : "move";
        return "draw";
    }

    function testHitMode(mouseX, mouseY) {
        return hitMode(mouseX, mouseY);
    }

    function cursorForMode(mode) {
        if (mode === "topLeft" || mode === "bottomRight")
            return Qt.SizeFDiagCursor;
        if (mode === "topRight" || mode === "bottomLeft")
            return Qt.SizeBDiagCursor;
        if (mode === "top" || mode === "bottom")
            return Qt.SizeVerCursor;
        if (mode === "left" || mode === "right")
            return Qt.SizeHorCursor;
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

        const ratio = constraintAspectRatio();
        if (ratio > 0) {
            if (width / Math.max(height, 1e-6) > ratio) {
                height = width / ratio;
            } else {
                width = height * ratio;
            }

            if (width > maxWidth) {
                width = maxWidth;
                height = width / ratio;
            }
            if (height > maxHeight) {
                height = maxHeight;
                width = height * ratio;
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

    function dragEdge(mouseX, mouseY, mode) {
        const current = normalizeForImage(mouseX, mouseY);
        const startRect = dragState.startRect;
        const ratio = constraintAspectRatio();
        if (mode === "left" || mode === "right") {
            const anchorX = mode === "left" ? startRect.x + startRect.width : startRect.x;
            const direction = mode === "left" ? -1 : 1;
            const maxWidth = direction > 0 ? 1 - anchorX : anchorX;
            let width = clamp(Math.abs(current.x - anchorX), minimumWidth(), maxWidth);
            let height = startRect.height;
            let y = startRect.y;
            if (ratio > 0) {
                const centerY = startRect.y + startRect.height / 2;
                const maxCenteredHeight = 2 * Math.min(centerY, 1 - centerY);
                width = Math.min(width, maxCenteredHeight * ratio);
                height = Math.max(minimumHeight(), width / ratio);
                width = height * ratio;
                y = clamp(centerY - height / 2, 0, 1 - height);
            }
            const x = direction > 0 ? anchorX : anchorX - width;
            emitRect(Qt.rect(x, y, width, height));
            return;
        }

        const anchorY = mode === "top" ? startRect.y + startRect.height : startRect.y;
        const direction = mode === "top" ? -1 : 1;
        const maxHeight = direction > 0 ? 1 - anchorY : anchorY;
        let height = clamp(Math.abs(current.y - anchorY), minimumHeight(), maxHeight);
        let width = startRect.width;
        let x = startRect.x;
        if (ratio > 0) {
            const centerX = startRect.x + startRect.width / 2;
            const maxCenteredWidth = 2 * Math.min(centerX, 1 - centerX);
            height = Math.min(height, maxCenteredWidth / ratio);
            width = Math.max(minimumWidth(), height * ratio);
            height = width / ratio;
            x = clamp(centerX - width / 2, 0, 1 - width);
        }
        const y = direction > 0 ? anchorY : anchorY - height;
        emitRect(Qt.rect(x, y, width, height));
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

        const ratio = constraintAspectRatio();
        if (ratio > 0) {
            if (width / Math.max(height, 1e-6) > ratio) {
                height = width / ratio;
            } else {
                width = height * ratio;
            }

            if (width > maxWidth) {
                width = maxWidth;
                height = width / ratio;
            }
            if (height > maxHeight) {
                height = maxHeight;
                width = height * ratio;
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
        if (dragState.mode === "top" || dragState.mode === "bottom" ||
                dragState.mode === "left" || dragState.mode === "right") {
            dragEdge(mouseX, mouseY, dragState.mode);
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
        property bool interactionActive: false
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
            border.width: 2
            border.color: frameColor
        }

        Rectangle {
            x: 10
            y: 10
            radius: 6
            color: "#96101419"
            border.width: 1
            border.color: "#2ef4f7fa"
            visible: root.visible
            implicitWidth: cropLabel.implicitWidth + 14
            implicitHeight: cropLabel.implicitHeight + 8

            Text {
                id: cropLabel
                anchors.centerIn: parent
                text: root.cropPixelSummary()
                color: "#f4f7fa"
                font.family: "Avenir Next"
                font.pixelSize: 10
                font.weight: Font.Medium
            }
        }

        Rectangle {
            width: 1
            height: parent.height
            x: parent.width / 3
            color: guideColor
            opacity: guideMode === "Thirds" ? guideLineOpacity : 0
        }

        Rectangle {
            width: 1
            height: parent.height
            x: parent.width * 2 / 3
            color: guideColor
            opacity: guideMode === "Thirds" ? guideLineOpacity : 0
        }

        Rectangle {
            width: parent.width
            height: 1
            y: parent.height / 3
            color: guideColor
            opacity: guideMode === "Thirds" ? guideLineOpacity : 0
        }

        Rectangle {
            width: parent.width
            height: 1
            y: parent.height * 2 / 3
            color: guideColor
            opacity: guideMode === "Thirds" ? guideLineOpacity : 0
        }

        Rectangle {
            width: 1
            height: parent.height
            x: parent.width / 2
            color: guideColor
            opacity: guideMode === "Center" ? guideLineOpacity : 0
        }

        Rectangle {
            width: parent.width
            height: 1
            y: parent.height / 2
            color: guideColor
            opacity: guideMode === "Center" ? guideLineOpacity : 0
        }

        Rectangle {
            width: 1
            height: parent.height
            x: parent.width * goldenMinor
            color: guideColor
            opacity: guideMode === "Golden Ratio" ? guideLineOpacity : 0
        }

        Rectangle {
            width: 1
            height: parent.height
            x: parent.width * goldenMajor
            color: guideColor
            opacity: guideMode === "Golden Ratio" ? guideLineOpacity : 0
        }

        Rectangle {
            width: parent.width
            height: 1
            y: parent.height * goldenMinor
            color: guideColor
            opacity: guideMode === "Golden Ratio" ? guideLineOpacity : 0
        }

        Rectangle {
            width: parent.width
            height: 1
            y: parent.height * goldenMajor
            color: guideColor
            opacity: guideMode === "Golden Ratio" ? guideLineOpacity : 0
        }

        Rectangle {
            width: Math.sqrt(parent.width * parent.width + parent.height * parent.height)
            height: 1
            x: 0
            y: 0
            color: guideColor
            opacity: guideMode === "Diagonal" ? guideLineOpacity : 0
            rotation: Math.atan2(parent.height, parent.width) * 180 / Math.PI
            transformOrigin: Item.Left
        }

        Rectangle {
            width: Math.sqrt(parent.width * parent.width + parent.height * parent.height)
            height: 1
            x: 0
            y: parent.height
            color: guideColor
            opacity: guideMode === "Diagonal" ? guideLineOpacity : 0
            rotation: -Math.atan2(parent.height, parent.width) * 180 / Math.PI
            transformOrigin: Item.Left
        }

        Canvas {
            id: goldenSpiralCanvas
            objectName: "cropGoldenSpiralCanvas"
            readonly property bool backingAllowed: root.visible &&
                                                      parent.width > 1 && parent.height > 1 &&
                                                      parent.width <= root.maximumCanvasBackingEdge &&
                                                      parent.height <= root.maximumCanvasBackingEdge
            x: 0
            y: 0
            width: backingAllowed ? parent.width : 1
            height: backingAllowed ? parent.height : 1
            opacity: guideMode === "Golden Spiral" ? guideSpiralOpacity : 0
            visible: opacity > 0 && backingAllowed

            function scheduleSafePaint() {
                if (visible && backingAllowed)
                    requestPaint()
            }

            onWidthChanged: scheduleSafePaint()
            onHeightChanged: scheduleSafePaint()
            onVisibleChanged: scheduleSafePaint()
            Connections {
                target: root
                function onGuideModeChanged() { goldenSpiralCanvas.scheduleSafePaint() }
            }

            onPaint: {
                if (!backingAllowed || width <= 1 || height <= 1)
                    return;
                const ctx = getContext("2d");
                ctx.reset();
                ctx.clearRect(0, 0, width, height);

                ctx.save();
                ctx.rect(0, 0, width, height);
                ctx.clip();
                ctx.strokeStyle = root.guideColor;
                ctx.lineWidth = 1;
                ctx.beginPath();

                const maxRadius = Math.min(width, height) * 0.92;
                const centerX = width * root.goldenMajor;
                const centerY = height * root.goldenMajor;
                const maxTheta = Math.PI * 2.45;
                const stepCount = 220;
                for (let step = 0; step <= stepCount; ++step) {
                    const theta = (step / stepCount) * maxTheta;
                    const radius = maxRadius * Math.pow(root.goldenRatio, (theta - maxTheta) / (Math.PI / 2));
                    const x = centerX - Math.cos(theta) * radius;
                    const y = centerY - Math.sin(theta) * radius;
                    if (step === 0)
                        ctx.moveTo(x, y);
                    else
                        ctx.lineTo(x, y);
                }

                ctx.stroke();
                ctx.restore();
            }
        }
    }

    Repeater {
        model: root.visible ? root.visualHandleNames : []

        delegate: Rectangle {
            required property string modelData
            objectName: "cropVisualHandle_" + modelData
            readonly property rect visualRect: root.visualHandleRect(modelData)

            x: visualRect.x
            y: visualRect.y
            width: visualRect.width
            height: visualRect.height
            radius: width > height ? height / 2 : width / 2
            color: "#e8edf2"
            border.width: 1
            border.color: root.accentColor
            visible: root.visible
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: cursorForMode(dragState.mode !== "" ? dragState.mode : hitMode(mouseX, mouseY))

        onPressed: function(mouse) {
            if (!pointInImage(mouse.x, mouse.y)) {
                mouse.accepted = false
                return;
            }

            dragState.mode = hitMode(mouse.x, mouse.y);
            dragState.interactionActive = true
            root.interactionStarted()
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

        onReleased: {
            dragState.mode = ""
            if (dragState.interactionActive) {
                dragState.interactionActive = false
                root.interactionFinished()
            }
        }
        onCanceled: {
            dragState.mode = ""
            if (dragState.interactionActive) {
                dragState.interactionActive = false
                root.interactionFinished()
            }
        }
    }
}
