import QtQuick

// Restore a context once, then let Flickable own its position during input.
// Persist only after settling, never through a contentY -> settings -> contentY binding.
Item {
    id: root
    visible: false
    required property Flickable view
    property real position: 0
    property bool restoring: false
    signal positionSettled(real position)

    function restore() {
        if (!view || !view.visible || view.moving || view.dragging)
            return
        restoring = true
        view.contentY = Math.max(view.originY, Math.min(view.originY + position,
            Math.max(view.originY, view.originY + view.contentHeight - view.height)))
        restoring = false
    }

    onPositionChanged: {
        if (view && Math.abs(view.contentY - view.originY - position) > 0.5)
            restoreTimer.restart()
    }
    Component.onCompleted: restoreTimer.start()

    Timer {
        id: restoreTimer
        interval: 0
        onTriggered: root.restore()
    }
    Timer {
        id: settleTimer
        interval: 180
        onTriggered: {
            if (!root.view || !root.view.visible)
                return
            if (root.view.moving || root.view.dragging) {
                restart()
                return
            }
            const offset = Math.max(0, root.view.contentY - root.view.originY)
            if (Math.abs(root.position - offset) > 0.5)
                root.positionSettled(offset)
        }
    }
    Connections {
        target: root.view
        function onContentYChanged() {
            if (!root.restoring && !restoreTimer.running && root.view.visible)
                settleTimer.restart()
        }
        function onHeightChanged() { restoreTimer.restart() }
        function onContentHeightChanged() {
            // A virtual list refines its height estimate while heterogeneous
            // rows enter view. Never pull an active gesture back to its saved start.
            if (!root.view.moving && !settleTimer.running)
                restoreTimer.restart()
        }
        function onVisibleChanged() {
            if (root.view.visible) restoreTimer.restart()
        }
    }
}
