import QtQuick

MouseArea {
    id: target
    required property Item canvasViewport
    signal activateRequested()
    property point pressPoint: Qt.point(0, 0)
    property point pressPan: Qt.point(0, 0)
    property bool panning: false
    hoverEnabled: true
    preventStealing: true
    acceptedButtons: Qt.LeftButton
    cursorShape: panning && pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
    onWheel: function(wheel) {
        const point = mapToItem(canvasViewport, wheel.x, wheel.y)
        canvasViewport.handleViewportWheel(wheel, point.x, point.y)
    }
    onPressed: function(mouse) {
        pressPoint = mapToItem(canvasViewport, mouse.x, mouse.y)
        pressPan = Qt.point(canvasViewport.panX, canvasViewport.panY)
        panning = false
    }
    onPositionChanged: function(mouse) {
        if (!pressed) return
        const point = mapToItem(canvasViewport, mouse.x, mouse.y)
        const dx = point.x - pressPoint.x
        const dy = point.y - pressPoint.y
        if (Math.sqrt(dx * dx + dy * dy) >= Qt.styleHints.startDragDistance) panning = true
        if (panning) {
            canvasViewport.panX = pressPan.x + dx
            canvasViewport.panY = pressPan.y + dy
        }
    }
    onClicked: if (!panning) activateRequested()
    onCanceled: panning = false
}
