import QtQuick

WheelHandler {
    id: root
    objectName: "timelineViewportVerticalWheelHandler"

    property var viewport: null
    property real timelineZoom: 1.0
    property real laneScale: 1.0
    property real angleStepPixels: 48
    signal zoomRequested(real newZoom, real viewportAnchorX)
    signal laneScaleRequested(real scale, real viewportAnchorY)
    // Read-only diagnostics expose only the most recent wheel packet. This is
    // intentionally not an event history or a device identity record.
    property int diagnosticEventCount: 0
    property var lastWheelEvent: ({ received: false })
    readonly property var diagnosticState: ({
        objectName: root.objectName,
        orientation: "vertical",
        enabled: root.enabled,
        acceptedDevices: Number(root.acceptedDevices),
        lastEvent: root.lastWheelEvent
    })

    orientation: Qt.Vertical
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
            orientation: "vertical",
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

        const horizontalDelta = deltaPixels(event.pixelDelta.x, event.angleDelta.x)
        const verticalDelta = deltaPixels(event.pixelDelta.y, event.angleDelta.y)
        if ((event.modifiers & Qt.AltModifier) !== 0
                && (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) === 0) {
            if (verticalDelta === 0) {
                event.accepted = false
                captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
                return
            }
            const viewportPoint = viewport.mapFromItem(null, point.scenePosition.x, point.scenePosition.y)
            laneScaleRequested(laneScale * (verticalDelta > 0 ? 1.08 : 1 / 1.08),
                               Math.max(0, Math.min(viewport.height, viewportPoint.y)))
            event.accepted = true
            captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
            return
        }
        if ((event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0) {
            // Ignore zero-delta phase/end events instead of treating them as
            // a negative wheel step. Trackpads can report only pixelDelta.
            if (verticalDelta === 0) {
                event.accepted = false
                captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
                return
            }
            const step = verticalDelta > 0 ? 1.15 : (1.0 / 1.15)
            // point.position follows Flickable's scrolled content coordinates.
            // Map its scene position back into the viewport before combining
            // it with contentX, so the scroll offset is counted only once.
            const viewportPoint = viewport.mapFromItem(
                        null, point.scenePosition.x, point.scenePosition.y)
            const anchorX = Math.max(0, Math.min(viewport.width, viewportPoint.x))
            zoomRequested(timelineZoom * step, viewport.contentX + anchorX)
            event.accepted = true
            captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
            return
        }

        if (horizontalDelta !== 0) {
            // Let the horizontal handler own diagonal input and apply both
            // axes once, rather than panning y here and y again there.
            event.accepted = false
            captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
            return
        }

        if ((event.modifiers & Qt.ShiftModifier) !== 0) {
            if (verticalDelta === 0) {
                event.accepted = false
                captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
                return
            }
            viewport.contentX = clampPosition(
                        viewport.contentX - verticalDelta,
                        viewport.contentWidth,
                        viewport.width)
            event.accepted = true
            captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
            return
        }

        if (verticalDelta === 0) {
            event.accepted = false
            captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
            return
        }

        // WheelHandler consumes ordinary vertical events on this viewport,
        // so apply their logical-pixel or notch delta explicitly.
        viewport.contentY = clampPosition(
                    viewport.contentY - verticalDelta,
                    viewport.contentHeight,
                    viewport.height)
        event.accepted = true
        captureWheelEvent(event, beforeX, beforeY, horizontalDelta, verticalDelta)
    }
}
