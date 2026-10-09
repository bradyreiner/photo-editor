import QtQuick

Item {
    id: root

    required property var points
    required property rect sourceBounds
    property var selectedPointIds: []
    property bool selectionEnabled: false
    property real minimumSeparation: 0.045
    property string selectionMessage: ""

    signal selectedPointsChanged(var pointIds)

    function pointById(id) {
        const values = points || []
        for (let index = 0; index < values.length; ++index) {
            if (String(values[index].id || "") === String(id))
                return values[index]
        }
        return null
    }

    function distanceBetween(left, right) {
        const dx = Number(left.x || 0) - Number(right.x || 0)
        const dy = Number(left.y || 0) - Number(right.y || 0)
        return Math.sqrt(dx * dx + dy * dy)
    }

    function triangleArea(first, second, third) {
        return Math.abs((first.x * (second.y - third.y)
                         + second.x * (third.y - first.y)
                         + third.x * (first.y - second.y)) / 2)
    }

    onPointsChanged: pointsCanvas.requestPaint()
    onSelectedPointIdsChanged: pointsCanvas.requestPaint()
    onSourceBoundsChanged: pointsCanvas.requestPaint()

    Canvas {
        id: pointsCanvas
        anchors.fill: parent
        renderTarget: Canvas.Image
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            const bounds = root.sourceBounds
            if (bounds.width <= 0 || bounds.height <= 0)
                return
            const values = root.points || []
            const selected = root.selectedPointIds || []
            for (let index = 0; index < values.length; ++index) {
                const point = values[index]
                if (!point || !isFinite(Number(point.x)) || !isFinite(Number(point.y)))
                    continue
                const x = bounds.x + Number(point.x) * bounds.width
                const y = bounds.y + Number(point.y) * bounds.height
                if (x < bounds.x || x > bounds.x + bounds.width
                        || y < bounds.y || y > bounds.y + bounds.height)
                    continue
                const id = String(point.id || "")
                const selectedIndex = selected.indexOf(id)
                context.beginPath()
                context.arc(x, y, selectedIndex >= 0 ? 8 : 3.4, 0, Math.PI * 2)
                context.fillStyle = selectedIndex >= 0 ? "#ffbf66" : "rgba(120, 218, 255, 0.82)"
                context.fill()
                if (selectedIndex >= 0) {
                    context.lineWidth = 2
                    context.strokeStyle = "#16232c"
                    context.stroke()
                    context.font = "600 11px sans-serif"
                    context.textAlign = "center"
                    context.textBaseline = "middle"
                    context.fillStyle = "#17212a"
                    context.fillText(String(selectedIndex + 1), x, y)
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.selectionEnabled
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        onClicked: function(mouse) {
            const bounds = root.sourceBounds
            if (bounds.width <= 0 || bounds.height <= 0
                    || mouse.x < bounds.x || mouse.x > bounds.x + bounds.width
                    || mouse.y < bounds.y || mouse.y > bounds.y + bounds.height)
                return

            const values = root.points || []
            const radiusSquared = 20 * 20
            let nearest = null
            let nearestDistanceSquared = radiusSquared
            for (let index = 0; index < values.length; ++index) {
                const point = values[index]
                if (!point || !isFinite(Number(point.x)) || !isFinite(Number(point.y)))
                    continue
                const dx = mouse.x - (bounds.x + Number(point.x) * bounds.width)
                const dy = mouse.y - (bounds.y + Number(point.y) * bounds.height)
                const distanceSquared = dx * dx + dy * dy
                if (distanceSquared < nearestDistanceSquared) {
                    nearestDistanceSquared = distanceSquared
                    nearest = point
                }
            }
            if (!nearest)
                return

            const id = String(nearest.id || "")
            let next = (root.selectedPointIds || []).slice(0)
            const oldIndex = next.indexOf(id)
            if (oldIndex >= 0) {
                next.splice(oldIndex, 1)
            } else if (next.length >= 3) {
                root.selectionMessage = "Three points are set. Click one to replace it."
                return
            } else {
                const candidate = {"x": Number(nearest.x), "y": Number(nearest.y)}
                for (let index = 0; index < next.length; ++index) {
                    const selected = root.pointById(next[index])
                    if (selected && root.distanceBetween(candidate, selected) < root.minimumSeparation) {
                        root.selectionMessage = "Choose points farther apart to define a stable plane."
                        return
                    }
                }
                if (next.length === 2) {
                    const first = root.pointById(next[0])
                    const second = root.pointById(next[1])
                    if (first && second
                            && root.triangleArea(first, second, candidate) < 0.0025) {
                        root.selectionMessage = "Choose a third point away from the line between the first two."
                        return
                    }
                }
                next.push(id)
            }
            root.selectionMessage = next.length === 3
                    ? "Three plane points selected. Attach this plane to a title or graphic."
                    : String(next.length) + " of 3 plane points selected."
            root.selectedPointsChanged(next)
            pointsCanvas.requestPaint()
        }
    }
}
