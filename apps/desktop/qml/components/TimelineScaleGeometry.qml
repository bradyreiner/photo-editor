import QtQml

QtObject {
    id: root

    readonly property real minimumZoom: 0.01
    readonly property real supportedZoomFloor: 0.000000000001

    function finiteNumber(value, fallback) {
        const number = Number(value)
        return isFinite(number) ? number : fallback
    }

    function effectiveZoom(zoom, minimum) {
        const floor = Math.max(supportedZoomFloor, finiteNumber(minimum, minimumZoom))
        return Math.max(floor, finiteNumber(zoom, floor))
    }

    function secondsPerPixel(zoom, minimum) {
        return 1 / effectiveZoom(zoom, minimum)
    }

    function zoomChangedEnough(previousZoom, nextZoom) {
        const previous = Math.max(supportedZoomFloor, finiteNumber(previousZoom, minimumZoom))
        const next = Math.max(supportedZoomFloor, finiteNumber(nextZoom, previous))
        const tolerance = Math.max(0.000000000000001, previous * 0.000001)
        return Math.abs(next - previous) > tolerance
    }

    function minimumZoomForRange(availableWidth, durationSeconds, preferredMinimum) {
        const preferred = Math.max(supportedZoomFloor,
                                   finiteNumber(preferredMinimum, minimumZoom))
        const duration = Math.max(0, finiteNumber(durationSeconds, 0))
        if (duration <= 0)
            return preferred
        const width = Math.max(0, finiteNumber(availableWidth, 0))
        if (width <= 0)
            return Math.min(preferred, supportedZoomFloor)
        return Math.max(supportedZoomFloor, Math.min(preferred, width / duration))
    }

    function fitZoom(availableWidth, durationSeconds, minimum, maximum) {
        const floor = Math.max(supportedZoomFloor, finiteNumber(minimum, minimumZoom))
        const ceiling = Math.max(floor, finiteNumber(maximum, 600))
        const duration = Math.max(0, finiteNumber(durationSeconds, 0))
        if (duration <= 0)
            return floor
        const width = Math.max(0, finiteNumber(availableWidth, 0))
        return Math.max(floor, Math.min(ceiling, width / duration))
    }

    function clipWidth(durationSeconds, zoom) {
        const duration = Math.max(0, finiteNumber(durationSeconds, 0))
        const pixelsPerSecond = Math.max(0, finiteNumber(zoom, 0))
        return duration * pixelsPerSecond
    }

    function hitAreaWidth(bodyWidth, minimumWidth) {
        return Math.max(0, finiteNumber(bodyWidth, 0), Math.max(0, finiteNumber(minimumWidth, 0)))
    }

    function containsSequenceTime(seconds, startSeconds, durationSeconds) {
        const position = finiteNumber(seconds, NaN)
        const start = finiteNumber(startSeconds, NaN)
        const duration = finiteNumber(durationSeconds, NaN)
        return isFinite(position) && isFinite(start) && isFinite(duration)
                && duration > 0 && position >= start && position < start + duration
    }
}
