pragma ComponentBehavior: Bound
import QtQuick

MouseArea {
    objectName: "sceneNavigationSurface"
    id: navigationSurface
    required property var navigation
    required property var viewport
    anchors.fill: parent
    enabled: Boolean(navigationSurface.viewport.status.frameAvailable) && !navigationSurface.navigation.cleanupSelectionArmed
    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
    property real previousX: 0
    property real previousY: 0
    property real pressX: 0
    property real pressY: 0
    property bool gesturePans: false
    property bool gestureOrbits: false
    property bool gestureRolls: false
    property bool gestureAltRollCandidate: false
    property bool gestureFocusPick: false
    property bool gestureOrbitPick: false
    property bool navigationStarted: false
    property bool movedCamera: false
    property real pendingNavigationDx: 0
    property real pendingNavigationDy: 0
    function flushPendingNavigation(): void {
        const dx = pendingNavigationDx
        const dy = pendingNavigationDy
        pendingNavigationDx = 0
        pendingNavigationDy = 0
        if (dx === 0 && dy === 0) return
        if (navigationSurface.navigation.displayedImageWidth <= 0 || navigationSurface.navigation.displayedImageHeight <= 0) return
        if (gestureRolls) navigationSurface.viewport.roll(dx / navigationSurface.navigation.displayedImageWidth * 180)
        else if (gesturePans) navigationSurface.viewport.pan(-dx / navigationSurface.navigation.displayedImageWidth, -dy / navigationSurface.navigation.displayedImageHeight)
        else if (gestureOrbits) navigationSurface.viewport.orbit(dx / navigationSurface.navigation.displayedImageWidth * 180, dy / navigationSurface.navigation.displayedImageHeight * 180)
        else navigationSurface.viewport.look(-dx / navigationSurface.navigation.displayedImageWidth * 180, -dy / navigationSurface.navigation.displayedImageHeight * 180)
    }
    Timer {
        id: navigationDeltaFlush
        interval: 16
        repeat: false
        onTriggered: navigationSurface.flushPendingNavigation()
    }
    cursorShape: navigationSurface.navigation.focusPickArmed || navigationSurface.navigation.orbitPickArmed ? Qt.CrossCursor : Qt.ArrowCursor
    onPressed: mouse => {
        navigationDeltaFlush.stop()
        pendingNavigationDx = 0
        pendingNavigationDy = 0
        pressX = previousX = mouse.x
        pressY = previousY = mouse.y
        gestureFocusPick = mouse.button === Qt.LeftButton && navigationSurface.navigation.focusPickArmed
        gestureAltRollCandidate = mouse.button === Qt.LeftButton
            && !navigationSurface.navigation.focusPickArmed
            && (mouse.modifiers & Qt.AltModifier) !== 0
        gestureOrbitPick = mouse.button === Qt.LeftButton && navigationSurface.navigation.orbitPickArmed
            && !gestureFocusPick && !gestureAltRollCandidate
        gestureRolls = mouse.button === Qt.LeftButton && !gestureFocusPick && !gestureOrbitPick
            && (gestureAltRollCandidate || (navigationSurface.navigation.navigationRollMode && (mouse.modifiers & Qt.ShiftModifier) === 0))
        gesturePans = !gestureRolls && ((mouse.modifiers & Qt.ShiftModifier) !== 0
            || mouse.button !== Qt.LeftButton || navigationSurface.navigation.navigationPanMode)
        gestureOrbits = mouse.button === Qt.LeftButton && !gestureRolls && !gesturePans
            && !gestureFocusPick && !gestureOrbitPick && navigationSurface.navigation.navigationOrbitMode
        navigationStarted = false
        movedCamera = false
        navigationSurface.viewport.forceActiveFocus(Qt.MouseFocusReason)
    }
    onReleased: {
        navigationDeltaFlush.stop()
        flushPendingNavigation()
        if (navigationStarted && typeof navigationSurface.viewport.endNavigation === "function") navigationSurface.viewport.endNavigation()
        navigationStarted = false
    }
    onCanceled: {
        navigationDeltaFlush.stop()
        flushPendingNavigation()
        if (navigationStarted && typeof navigationSurface.viewport.endNavigation === "function") navigationSurface.viewport.endNavigation()
        navigationStarted = false
        gestureFocusPick = false
        gestureOrbitPick = false
        gestureAltRollCandidate = false
        gestureRolls = false
        gestureOrbits = false
    }
    onClicked: mouse => {
        if (navigationStarted || movedCamera || mouse.button !== Qt.LeftButton) {
            gestureFocusPick = false
            gestureOrbitPick = false
            gestureAltRollCandidate = false
            gestureRolls = false
            gestureOrbits = false
            movedCamera = false
            return
        }
        if (navigationSurface.navigation.focusPickArmed || gestureFocusPick || gestureAltRollCandidate) {
            const result = navigationSurface.viewport.pickFocusAt(mouse.x, mouse.y)
            navigationSurface.navigation.actionError = String(result.error || "")
            if (result.ok) {
                navigationSurface.navigation.focusPickArmed = false
                if (gestureAltRollCandidate) navigationSurface.navigation.orbitPickArmed = false
            }
        } else if (navigationSurface.navigation.orbitPickArmed || gestureOrbitPick) {
            const result = navigationSurface.viewport.orbitAroundAt(mouse.x, mouse.y)
            navigationSurface.navigation.actionError = String(result.error || "")
            if (result.ok) {
                navigationSurface.navigation.orbitPickArmed = false
                navigationSurface.navigation.navigationOrbitMode = true
                navigationSurface.navigation.navigationPanMode = false
                navigationSurface.navigation.navigationRollMode = false
            }
        }
        gestureFocusPick = false
        gestureOrbitPick = false
        gestureAltRollCandidate = false
        gestureRolls = false
        gestureOrbits = false
    }
    onPositionChanged: mouse => {
        if (!pressed || gestureFocusPick || gestureOrbitPick) return
        if (!navigationStarted) {
            if (Math.hypot(mouse.x - pressX, mouse.y - pressY) < 3) return
            if (typeof navigationSurface.viewport.beginNavigation === "function") navigationSurface.viewport.beginNavigation()
            navigationStarted = true
            movedCamera = true
        }
        const dx = mouse.x - previousX
        const dy = mouse.y - previousY
        pendingNavigationDx += dx
        pendingNavigationDy += dy
        previousX = mouse.x
        previousY = mouse.y
        if (!navigationDeltaFlush.running) navigationDeltaFlush.start()
    }
    onWheel: wheel => {
        const usePixelDelta = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
        if ((wheel.modifiers & Qt.ShiftModifier) !== 0 || navigationSurface.navigation.navigationPanMode) {
            const panX = usePixelDelta ? wheel.pixelDelta.x / navigationSurface.navigation.displayedImageWidth : wheel.angleDelta.x / 120 * 0.025
            const panY = usePixelDelta ? wheel.pixelDelta.y / navigationSurface.navigation.displayedImageHeight : wheel.angleDelta.y / 120 * 0.025
            navigationSurface.viewport.pan(-panX, -panY)
        } else {
            const steps = usePixelDelta ? wheel.pixelDelta.y / 600 : wheel.angleDelta.y / 120 * 0.3
            if (steps !== 0) {
                if (navigationSurface.navigation.navigationOrbitMode) navigationSurface.viewport.dolly(steps)
                else navigationSurface.viewport.moveCamera(0, 0, steps * navigationSurface.navigation.navigationMoveStep * 6)
            }
        }
        wheel.accepted = true
    }
    onDoubleClicked: mouse => {
        if (!navigationSurface.navigation.orbitPickArmed && !navigationSurface.navigation.focusPickArmed && !navigationSurface.viewport.status.focusPicking) {
            navigationSurface.navigation.navigationOrbitMode = true
            navigationSurface.navigation.navigationPanMode = false
            navigationSurface.navigation.navigationRollMode = false
            navigationSurface.navigation.actionError = String(navigationSurface.viewport.orbitAroundAt(mouse.x, mouse.y).error || "")
        }
    }
}
