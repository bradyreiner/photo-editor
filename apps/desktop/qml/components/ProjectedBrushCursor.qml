pragma ComponentBehavior: Bound
import QtQuick

// The ring follows the same inverse/forward source mapping as the rasterizer.
// Keep hover painting separate from the expensive static selection canvases.
Item {
    id: root
    enabled: false
    required property var controller
    required property real pointerX
    required property real pointerY
    required property real imageX
    required property real imageY
    required property real imageWidth
    required property real imageHeight
    required property real brushSize
    required property real hardness
    property color accent: "white"
    property int geometryRevision: 0
    readonly property bool available: controller && typeof controller.brushCursorContour === "function"
    readonly property var outer: contour(brushSize)
    readonly property var inner: contour(brushSize * hardness)
    readonly property rect bounds: contourBounds(outer)

    function contour(radius) {
        const revision = geometryRevision
        if (!visible || !available || imageWidth <= 0 || imageHeight <= 0 || radius <= 0)
            return []
        return controller.brushCursorContour((pointerX - imageX) / imageWidth,
                                             (pointerY - imageY) / imageHeight, radius)
    }
    function contourBounds(points) {
        if (points.length === 0)
            return Qt.rect(0, 0, 0, 0)
        let left = width, top = height, right = 0, bottom = 0
        for (let i = 0; i < points.length; ++i) {
            const p = points[i]
            if (!p.valid)
                continue
            const x = imageX + p.x * imageWidth
            const y = imageY + p.y * imageHeight
            left = Math.min(left, x); right = Math.max(right, x)
            top = Math.min(top, y); bottom = Math.max(bottom, y)
        }
        left = Math.max(0, Math.floor(left - 3)); top = Math.max(0, Math.floor(top - 3))
        right = Math.min(width, Math.ceil(right + 3)); bottom = Math.min(height, Math.ceil(bottom + 3))
        return Qt.rect(left, top, Math.max(0, right - left), Math.max(0, bottom - top))
    }
    function repaint() { ring.requestPaint() }
    onOuterChanged: repaint()
    onInnerChanged: repaint()
    onBoundsChanged: repaint()
    onAccentChanged: repaint()
    onVisibleChanged: if (visible) repaint()
    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onControlsChanged() { root.geometryRevision += 1 }
        function onPreviewChanged() { root.geometryRevision += 1 }
        function onActiveBrushStrokeChanged() { root.geometryRevision += 1 }
        function onBrushStrokeActiveChanged() { root.geometryRevision += 1 }
    }
    Canvas {
        id: ring
        x: root.bounds.x; y: root.bounds.y
        width: root.bounds.width; height: root.bounds.height
        visible: root.available && width > 0 && height > 0
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            function path(points) {
                ctx.beginPath()
                let started = false
                for (let i = 0; i < points.length; ++i) {
                    const p = points[i]
                    if (!p.valid) { started = false; continue }
                    const px = root.imageX + p.x * root.imageWidth - ring.x
                    const py = root.imageY + p.y * root.imageHeight - ring.y
                    if (!started) { ctx.moveTo(px, py); started = true }
                    else ctx.lineTo(px, py)
                }
            }
            path(root.outer)
            ctx.lineWidth = 3; ctx.strokeStyle = "#eb101419"; ctx.stroke()
            ctx.lineWidth = 1.5; ctx.strokeStyle = root.accent; ctx.stroke()
            if (root.hardness > 0) {
                path(root.inner)
                ctx.lineWidth = 1.5; ctx.strokeStyle = "#ebf4f7fa"; ctx.stroke()
            }
        }
    }
}
