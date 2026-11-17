import QtQuick

QtObject {
    id: root

    property real contentX: 0
    property real contentY: 0
    property real contentWidth: 0
    property real contentHeight: 0
    property real cropX: 0
    property real cropY: 0
    property real cropWidth: 1
    property real cropHeight: 1
    property real rotationDegrees: 0
    // The controller delegates to the exact engine sampler. Keep the standalone
    // affine fallback for components/tests without a loaded image.
    property var geometryMapper: null
    property string coordinateSpace: "source"
    property string inputCoordinateSpace: coordinateSpace
    // Invokable C++ calls are opaque to QML's dependency tracker. Publish a
    // revision so bound handles/outlines move when geometry or the mask changes.
    property int geometryRevision: 0
    property Connections geometryChanges: Connections {
        target: root.geometryMapper
        ignoreUnknownSignals: true
        function onControlsChanged() { root.geometryRevision += 1 }
        function onPreviewChanged() { root.geometryRevision += 1 }
        function onActiveBrushStrokeChanged() { root.geometryRevision += 1 }
    }

    property real devicePixelRatio: 1
    property string fitMode: "fit"

    readonly property string coordinateTransformContract: "coordinate-transform:v1|view-to-image:true|image-to-view:true|crop:true|rotation:true|zoom-pan:true|painted-image-rect:true|fit-fill:true|device-pixel-ratio:true|roundtrip:true|clamp:true"
    readonly property bool contentValid: contentWidth > 1 && contentHeight > 1 && cropWidth > 0 && cropHeight > 0

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, Number(value)))
    }

    function rotateUnitPoint(x, y, degrees) {
        const radians = Number(degrees) * Math.PI / 180.0
        const aspectWidth = Math.max(1, Number(contentWidth))
        const aspectHeight = Math.max(1, Number(contentHeight))
        const dx = (Number(x) - 0.5) * aspectWidth
        const dy = (Number(y) - 0.5) * aspectHeight
        const cosValue = Math.cos(radians)
        const sinValue = Math.sin(radians)
        return {
            x: 0.5 + (dx * cosValue - dy * sinValue) / aspectWidth,
            y: 0.5 + (dx * sinValue + dy * cosValue) / aspectHeight
        }
    }

    function imageToViewPoint(normalizedX, normalizedY) {
        if (!contentValid)
            return { valid: false, x: 0, y: 0, normalizedX: 0, normalizedY: 0 }
        if (geometryRevision >= 0 && geometryMapper && geometryMapper.mapCanvasCoordinate && coordinateSpace !== "output") {
            const mapped = geometryMapper.mapCanvasCoordinate(Number(normalizedX), Number(normalizedY), true, coordinateSpace)
            return { valid: mapped.valid === true,
                     x: contentX + Number(mapped.x) * contentWidth,
                     y: contentY + Number(mapped.y) * contentHeight,
                     normalizedX: Number(normalizedX), normalizedY: Number(normalizedY) }
        }
        if (coordinateSpace === "output")
            return { valid: normalizedX >= 0 && normalizedX <= 1 && normalizedY >= 0 && normalizedY <= 1,
                     x: contentX + Number(normalizedX) * contentWidth, y: contentY + Number(normalizedY) * contentHeight,
                     normalizedX: Number(normalizedX), normalizedY: Number(normalizedY) }
        const imageX = Number(normalizedX)
        const imageY = Number(normalizedY)
        const cropLocalX = (imageX - cropX) / cropWidth
        const cropLocalY = (imageY - cropY) / cropHeight
        const rotated = rotateUnitPoint(cropLocalX, cropLocalY, rotationDegrees)
        return {
            valid: cropLocalX >= 0 && cropLocalX <= 1 && cropLocalY >= 0 && cropLocalY <= 1,
            x: contentX + rotated.x * contentWidth,
            y: contentY + rotated.y * contentHeight,
            normalizedX: clamp(imageX, 0, 1),
            normalizedY: clamp(imageY, 0, 1)
        }
    }

    function viewToImagePoint(viewX, viewY) {
        if (!contentValid)
            return { valid: false, x: 0, y: 0, viewX: 0, viewY: 0 }
        const viewLocalX = (Number(viewX) - contentX) / contentWidth
        const viewLocalY = (Number(viewY) - contentY) / contentHeight
        if (geometryRevision >= 0 && geometryMapper && geometryMapper.mapCanvasCoordinate && inputCoordinateSpace !== "output") {
            const mapped = geometryMapper.mapCanvasCoordinate(viewLocalX, viewLocalY, false, inputCoordinateSpace)
            return { valid: mapped.valid === true, x: Number(mapped.x), y: Number(mapped.y),
                     viewX: Number(viewX), viewY: Number(viewY) }
        }
        if (inputCoordinateSpace === "output")
            return { valid: viewLocalX >= 0 && viewLocalX <= 1 && viewLocalY >= 0 && viewLocalY <= 1,
                     x: clamp(viewLocalX, 0, 1), y: clamp(viewLocalY, 0, 1), viewX: Number(viewX), viewY: Number(viewY) }
        const unrotated = rotateUnitPoint(viewLocalX, viewLocalY, -rotationDegrees)
        const imageX = cropX + unrotated.x * cropWidth
        const imageY = cropY + unrotated.y * cropHeight
        return {
            valid: viewLocalX >= 0 && viewLocalX <= 1 && viewLocalY >= 0 && viewLocalY <= 1 &&
                   unrotated.x >= 0 && unrotated.x <= 1 && unrotated.y >= 0 && unrotated.y <= 1,
            x: clamp(imageX, 0, 1),
            y: clamp(imageY, 0, 1),
            viewX: Number(viewX),
            viewY: Number(viewY)
        }
    }

    function imageToViewRect(normalizedX, normalizedY, normalizedWidth, normalizedHeight) {
        const left = Number(normalizedX)
        const top = Number(normalizedY)
        const right = left + Number(normalizedWidth)
        const bottom = top + Number(normalizedHeight)
        const points = [
            imageToViewPoint(left, top),
            imageToViewPoint(right, top),
            imageToViewPoint(right, bottom),
            imageToViewPoint(left, bottom)
        ]
        let minX = points[0].x
        let minY = points[0].y
        let maxX = points[0].x
        let maxY = points[0].y
        let valid = true
        for (let index = 0; index < points.length; ++index) {
            minX = Math.min(minX, points[index].x)
            minY = Math.min(minY, points[index].y)
            maxX = Math.max(maxX, points[index].x)
            maxY = Math.max(maxY, points[index].y)
            valid = valid && points[index].valid
        }
        return { valid: valid, x: minX, y: minY, width: maxX - minX, height: maxY - minY }
    }

    function viewToImageRect(viewX, viewY, viewWidth, viewHeight) {
        const left = Number(viewX)
        const top = Number(viewY)
        const right = left + Number(viewWidth)
        const bottom = top + Number(viewHeight)
        const points = [
            viewToImagePoint(left, top),
            viewToImagePoint(right, top),
            viewToImagePoint(right, bottom),
            viewToImagePoint(left, bottom)
        ]
        let minX = points[0].x
        let minY = points[0].y
        let maxX = points[0].x
        let maxY = points[0].y
        let valid = true
        for (let index = 0; index < points.length; ++index) {
            minX = Math.min(minX, points[index].x)
            minY = Math.min(minY, points[index].y)
            maxX = Math.max(maxX, points[index].x)
            maxY = Math.max(maxY, points[index].y)
            valid = valid && points[index].valid
        }
        return { valid: valid, x: minX, y: minY, width: maxX - minX, height: maxY - minY }
    }

    function pointerMappingDiagnosticSignature(viewX, viewY) {
        const point = viewToImagePoint(viewX, viewY)
        return "canvas-coordinate-mapper:v1" +
               "|valid:" + String(point.valid === true) +
               "|view:" + Number(viewX || 0).toFixed(3) + "," +
               Number(viewY || 0).toFixed(3) +
               "|image:" + Number(point.x || 0).toFixed(6) + "," +
               Number(point.y || 0).toFixed(6) +
               "|content:" + Number(contentX || 0).toFixed(3) + "," +
               Number(contentY || 0).toFixed(3) + "," +
               Number(contentWidth || 0).toFixed(3) + "x" +
               Number(contentHeight || 0).toFixed(3) +
               "|crop:" + Number(cropX || 0).toFixed(6) + "," +
               Number(cropY || 0).toFixed(6) + "," +
               Number(cropWidth || 0).toFixed(6) + "x" +
               Number(cropHeight || 0).toFixed(6) +
               "|rotation:" + Number(rotationDegrees || 0).toFixed(3) +
               "|fitMode:" + String(fitMode || "fit") +
               "|dpr:" + Math.max(1.0, Number(devicePixelRatio || 1.0)).toFixed(3)
    }

    function devicePixelRectForViewRect(viewX, viewY, viewWidth, viewHeight) {
        const dpr = Math.max(1.0, Number(devicePixelRatio || 1.0))
        return {
            x: Math.round(Number(viewX) * dpr),
            y: Math.round(Number(viewY) * dpr),
            width: Math.round(Number(viewWidth) * dpr),
            height: Math.round(Number(viewHeight) * dpr),
            devicePixelRatio: dpr
        }
    }
}
