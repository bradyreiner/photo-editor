pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// Read-only viewer chrome: map the actual rendered element, rather than its
// timeline box. This item never participates in hit testing or export.
Item {
    id: root
    property Item targetItem: null
    property Item projectionItem: targetItem
    property rect targetRect: targetItem ? Qt.rect(0, 0, targetItem.width, targetItem.height) : Qt.rect(0, 0, 0, 0)
    property bool active: false
    property color outlineColor: "#c8f4f1"
    property real motionRevision: 0
    property real perspectiveX: 0
    property real perspectiveY: 0
    property var sceneQuad: []
    readonly property var corners: mappedCorners()
    readonly property bool hasOutline: active && corners.length === 4
    enabled: false
    visible: hasOutline
    Accessible.ignored: true

    function projectPoint(point) {
        // Program shaders expose TL, TR, BL, BR; the projective solve below
        // traverses the perimeter TL, TR, BR, BL.
        const q = sceneQuad && sceneQuad.length === 4
                ? [sceneQuad[0], sceneQuad[1], sceneQuad[3], sceneQuad[2]] : [
            {x: perspectiveX, y: perspectiveY}, {x: 1 - perspectiveX, y: perspectiveY},
            {x: 1, y: 1}, {x: 0, y: 1}]
        const dx1 = q[1].x - q[2].x, dx2 = q[3].x - q[2].x
        const dy1 = q[1].y - q[2].y, dy2 = q[3].y - q[2].y
        const dx3 = q[0].x - q[1].x + q[2].x - q[3].x
        const dy3 = q[0].y - q[1].y + q[2].y - q[3].y
        const denominator = dx1 * dy2 - dx2 * dy1
        const gx = Math.abs(denominator) > .00001 ? (dx3 * dy2 - dx2 * dy3) / denominator : 0
        const gy = Math.abs(denominator) > .00001 ? (dx1 * dy3 - dx3 * dy1) / denominator : 0
        const u = point.x / projectionItem.width, v = point.y / projectionItem.height
        const w = gx * u + gy * v + 1
        if (Math.abs(w) < .00001) return Qt.point(NaN, NaN)
        return Qt.point(((q[1].x - q[0].x + gx * q[1].x) * u
                         + (q[3].x - q[0].x + gy * q[3].x) * v + q[0].x) / w * projectionItem.width,
                        ((q[1].y - q[0].y + gx * q[1].y) * u
                         + (q[3].y - q[0].y + gy * q[3].y) * v + q[0].y) / w * projectionItem.height)
    }

    function mappedCorners() {
        motionRevision // Animated ancestors refresh only with the existing viewer clock.
        if (!active || !targetItem || targetRect.width <= 0 || targetRect.height <= 0)
            return []
        // Read geometry explicitly: mapToItem alone does not establish QML
        // binding dependencies on a transformed ancestor.
        for (let ancestor = targetItem; ancestor; ancestor = ancestor.parent) {
            const geometry = [ancestor.x, ancestor.y, ancestor.width, ancestor.height,
                              ancestor.scale, ancestor.rotation, ancestor.transformOrigin]
            if (!ancestor.visible || ancestor.opacity <= .001 || geometry.some(function(v) { return !isFinite(v) }))
                return []
        }
        const r = targetRect
        const points = [Qt.point(r.x, r.y), Qt.point(r.x + r.width, r.y),
                        Qt.point(r.x + r.width, r.y + r.height), Qt.point(r.x, r.y + r.height)]
        return points.map(function(point) {
            if (projectionItem && (Math.abs(perspectiveX) > .000001 || Math.abs(perspectiveY) > .000001
                                   || (sceneQuad && sceneQuad.length === 4))) {
                const projected = projectPoint(targetItem.mapToItem(projectionItem, point.x, point.y))
                return projectionItem.mapToItem(root, projected.x, projected.y)
            }
            return targetItem.mapToItem(root, point.x, point.y)
        }).filter(function(point) { return isFinite(point.x) && isFinite(point.y) })
    }

    Shape {
        anchors.fill: parent
        antialiasing: true
        ShapePath {
            strokeColor: "#c0000000"
            strokeWidth: 3
            fillColor: "transparent"
            startX: root.hasOutline ? root.corners[0].x : 0
            startY: root.hasOutline ? root.corners[0].y : 0
            PathLine { x: root.hasOutline ? root.corners[1].x : 0; y: root.hasOutline ? root.corners[1].y : 0 }
            PathLine { x: root.hasOutline ? root.corners[2].x : 0; y: root.hasOutline ? root.corners[2].y : 0 }
            PathLine { x: root.hasOutline ? root.corners[3].x : 0; y: root.hasOutline ? root.corners[3].y : 0 }
            PathLine { x: root.hasOutline ? root.corners[0].x : 0; y: root.hasOutline ? root.corners[0].y : 0 }
        }
        ShapePath {
            strokeColor: root.outlineColor
            strokeWidth: 1.25
            fillColor: "transparent"
            startX: root.hasOutline ? root.corners[0].x : 0
            startY: root.hasOutline ? root.corners[0].y : 0
            PathLine { x: root.hasOutline ? root.corners[1].x : 0; y: root.hasOutline ? root.corners[1].y : 0 }
            PathLine { x: root.hasOutline ? root.corners[2].x : 0; y: root.hasOutline ? root.corners[2].y : 0 }
            PathLine { x: root.hasOutline ? root.corners[3].x : 0; y: root.hasOutline ? root.corners[3].y : 0 }
            PathLine { x: root.hasOutline ? root.corners[0].x : 0; y: root.hasOutline ? root.corners[0].y : 0 }
        }
    }
}
