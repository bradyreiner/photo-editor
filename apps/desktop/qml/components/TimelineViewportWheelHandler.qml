import QtQuick

WheelHandler {
    id: root
    objectName: "timelineViewportHorizontalWheelHandler"

    property var viewport: null
    property real angleStepPixels: 48
    // Read-only diagnostics expose only the most recent wheel packet. This is
    // intentionally not an event history or a device identity record.
    property int diagnosticEventCount: 0
    property var lastWheelEvent: ({ received: false })
    readonly property var diagnosticState: ({
        objectName: root.objectName,
        orientation: "horizontal",
        enabled: root.enabled,
        acceptedDevices: Number(root.acceptedDevices),
        lastEvent: root.lastWheelEvent
    })

    orientation: Qt.Horizontal
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    target: null
    enabled: viewport !== null

    function deltaPixels(pixelDelta, angleDelta) {
        const pixelValue = Number(pixelDelta || 0)
        if (pixelValue !== 0)
            return pixelValue
        const angleValue = Number(angleDelta || 0)
        return angleValue === 0 ? 0 : angleValue / 120 * angleStepPixels
    }

    function clampPosition(value, contentExtent, viewportExtent) {
        const maximum = Math.max(0, Number(contentExtent) - Number(viewportExtent))
        return Math.max(0, Math.min(maximum, Number(value)))
    }

    function pointerDeviceName(deviceType) {
        if (deviceType === PointerDevice.Mouse) return "mouse"
        if (deviceType === PointerDevice.TouchPad) return "touchpad"
        if (deviceType === PointerDevice.TouchScreen) return "touchscreen"
        if (deviceType === PointerDevice.Stylus) return "stylus"
        if (deviceType === PointerDevice.Airbrush) return "airbrush"
        if (deviceType === PointerDevice.Puck) return "puck"
        return "unknown"
    }

    function captureWheelEvent(event, beforeX, beforeY, logicalX, logicalY) {
        const scene = point.scenePosition
        const viewportPoint = viewport
                ? viewport.mapFromItem(null, scene.x, scene.y)
                : Qt.point(0, 0)
        const viewportOrigin = viewport
                ? viewport.mapToItem(null, 0, 0)
                : Qt.point(0, 0)
        const rawDeviceType = point.device ? point.device.deviceType : -1
        const deviceType = Number.isFinite(Number(rawDeviceType))
                ? Number(rawDeviceType) : -1
        const deviceTypeName = pointerDeviceName(deviceType)
        diagnosticEventCount += 1
        lastWheelEvent = ({
            received: true,
            sequence: diagnosticEventCount,
            receivedAtMs: Date.now(),
            orientation: "horizontal",
            enabled: root.enabled,
            accepted: event.accepted,
            deviceType: deviceType,
            deviceTypeName: deviceTypeName,
            pixelDeltaX: Number(event.pixelDelta.x),
            pixelDeltaY: Number(event.pixelDelta.y),
            angleDeltaX: Number(event.angleDelta.x),
            angleDeltaY: Number(event.angleDelta.y),
            logicalDeltaX: logicalX,
            logicalDeltaY: logicalY,
            modifiers: Number(event.modifiers),
            inverted: Boolean(event.inverted),
            eventX: Number(event.x),
            eventY: Number(event.y),
            sceneX: Number(scene.x),
            sceneY: Number(scene.y),
            mappedViewportX: Number(viewportPoint.x),
            mappedViewportY: Number(viewportPoint.y),
            viewport: {
                sceneX: Number(viewportOrigin.x),
                sceneY: Number(viewportOrigin.y),
                width: viewport ? Number(viewport.width) : 0,
                height: viewport ? Number(viewport.height) : 0,
                visible: viewport ? Boolean(viewport.visible) : false,
                enabled: viewport ? Boolean(viewport.enabled) : false,
                contentWidth: viewport ? Number(viewport.contentWidth) : 0,
                contentHeight: viewport ? Number(viewport.contentHeight) : 0,
                contentXBefore: beforeX,
                contentYBefore: beforeY,
                contentXAfter: viewport ? Number(viewport.contentX) : beforeX,
                contentYAfter: viewport ? Number(viewport.contentY) : beforeY
            }
        })
    }

    onWheel: function(event) {
        const beforeX = viewport ? Number(viewport.contentX) : 0
        const beforeY = viewport ? Number(viewport.contentY) : 0
        if (!viewport) {
            event.accepted = false
            captureWheelEvent(event, beforeX, beforeY, 0, 0)
            return
        }

        if ((event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier)) !== 0) {
            // The vertical handler owns Ctrl/Command zoom.
            event.accepted = false
            captureWheelEvent(event, beforeX, beforeY,
                              deltaPixels(event.pixelDelta.x, event.angleDelta.x),
                              deltaPixels(event.pixelDelta.y, event.angleDelta.y))
            return
        }

        let horizontalDelta = deltaPixels(event.pixelDelta.x, event.angleDelta.x)
        let verticalDelta = deltaPixels(event.pixelDelta.y, event.angleDelta.y)
        if ((event.modifiers & Qt.ShiftModifier) !== 0) {
            verticalDelta = 0
        }

        if (horizontalDelta === 0) {
            // Let Flickable retain its normal vertical-wheel behavior.
            event.accepted = false
            captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
            return
        }

        viewport.contentX = clampPosition(
                    viewport.contentX - horizontalDelta,
                    viewport.contentWidth,
                    viewport.width)
        if (verticalDelta !== 0) {
            // A diagonal trackpad event has both axes. Once we consume its
            // horizontal component, apply its vertical component as well.
            viewport.contentY = clampPosition(
                        viewport.contentY - verticalDelta,
                        viewport.contentHeight + Number(viewport.bottomMargin || 0),
                        viewport.height)
        }
        event.accepted = true
        captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
    }
}
