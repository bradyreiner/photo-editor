import QtQuick

MouseArea {
    id: inputSurface

    required property Item workspace
    required property Item viewport
    required property var previewController

    objectName: viewport.objectName.length > 0 ? viewport.objectName + "PanZoomInputHandler" : "panZoomInputHandler"
    readonly property string inputHandlerRole: "pan-zoom"
    readonly property string pointerGrabPolicy: viewport.masked ? "defer-to-mask-overlay" : "viewport-pan-zoom"
    acceptedButtons: Qt.LeftButton
    hoverEnabled: true
    preventStealing: false
    cursorShape: workspace.pixelExactToolReady() || workspace.brushToolReady()
                 ? Qt.CrossCursor
                 : (workspace.spacePanActive || viewport.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor)
    property real pressX: 0
    property real pressY: 0
    property real startPanX: 0
    property real startPanY: 0

    onWheel: function(wheel) {
        const zoomFactor = viewport.wheelZoomFactor(wheel)
        if (Math.abs(zoomFactor - 1.0) > 0.0001)
            viewport.setZoom(viewport.zoom * zoomFactor, wheel.x, wheel.y)
        wheel.accepted = true
    }

    onPressed: function(mouse) {
        if (workspace.spacePanActive) {
            pressX = mouse.x
            pressY = mouse.y
            startPanX = viewport.panX
            startPanY = viewport.panY
            mouse.accepted = true
            cursorShape = Qt.ClosedHandCursor
            return
        }
        if (viewport.handleSamplingPointerPressed(mouse)) {
            mouse.accepted = true
            cursorShape = workspace.maskLuminanceRangeSampling || workspace.pixelExactToolReady()
                          ? Qt.CrossCursor
                          : (viewport.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor)
            return
        }
        if (viewport.handleMaskPointerPressed(mouse)) {
            mouse.accepted = true
            cursorShape = Qt.CrossCursor
            return
        }
        pressX = mouse.x
        pressY = mouse.y
        workspace.loupeX = mouse.x
        workspace.loupeY = mouse.y
        workspace.beginPixelInspectorSampling()
        viewport.updatePixelInspectorAt(mouse.x, mouse.y)
        startPanX = viewport.panX
        startPanY = viewport.panY
        cursorShape = workspace.pixelExactToolReady() || workspace.brushToolReady()
                      ? Qt.CrossCursor
                      : (viewport.zoom > 1.0 ? Qt.ClosedHandCursor : Qt.ArrowCursor)
    }

    onPositionChanged: function(mouse) {
        viewport.pointerX = mouse.x
        viewport.pointerY = mouse.y
        workspace.loupeX = mouse.x
        workspace.loupeY = mouse.y
        if (pressed && workspace.spacePanActive) {
            if (viewport.zoom > 1.0) {
                viewport.panX = startPanX + mouse.x - pressX
                viewport.panY = startPanY + mouse.y - pressY
            }
            mouse.accepted = true
            return
        }
        if (pressed && viewport.handleSamplingPointerMoved(mouse)) {
            mouse.accepted = true
            return
        }
        if (viewport.handleMaskPointerMoved(mouse)) {
            mouse.accepted = true
            return
        }
        if (pressed && workspace.pixelInspectorSamplingActive)
            viewport.updatePixelInspectorAt(mouse.x, mouse.y)
        if (!pressed || viewport.zoom <= 1.0)
            return
        viewport.panX = startPanX + mouse.x - pressX
        viewport.panY = startPanY + mouse.y - pressY
    }

    onExited: workspace.finishPixelInspectorSampling()
    onReleased: function(mouse) {
        if (workspace.spacePanActive) {
            cursorShape = Qt.OpenHandCursor
            mouse.accepted = true
            return
        }
        if (viewport.handleSamplingPointerReleased(mouse)) {
            cursorShape = workspace.pixelExactToolReady() || workspace.brushToolReady()
                          ? Qt.CrossCursor
                          : (viewport.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor)
            return
        }
        viewport.handleMaskPointerReleased()
        workspace.finishPixelInspectorSampling()
        cursorShape = workspace.pixelExactToolReady() || workspace.brushToolReady()
                      ? Qt.CrossCursor
                      : (viewport.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor)
    }
    onCanceled: {
        if (workspace.maskLuminanceRangeSampling)
            previewController.cancelActiveLuminanceRangeSample()
        viewport.handleMaskPointerReleased()
        workspace.finishPixelInspectorSampling()
        cursorShape = workspace.pixelExactToolReady() || workspace.brushToolReady()
                      ? Qt.CrossCursor
                      : (viewport.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor)
    }
    onDoubleClicked: {
        if (workspace.completeActivePolygonSelection())
            return
        viewport.resetView()
    }
}
