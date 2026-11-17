.pragma library

// Trackpad scrolling pans. Mouse wheels, pinches and modified scrolling zoom.
function wheelAction(boardMode, zoomModifier, pixelX, pixelY, angleX, angleY) {
    const precise = Math.abs(pixelX) + Math.abs(pixelY) > 0
    if (!boardMode || zoomModifier || !precise) return { zoom: true, x: 0, y: 0 }
    return { zoom: false, x: pixelX, y: pixelY }
}
