pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

FocusScope {
    id: root
    required property var theme
    property var channels: []
    property var parameters: []
    property string targetId: ""
    property string currentParameter: ""
    property var selectedSeconds: []
    property real duration: 1
    property real timeStart: 0
    property real timeSpan: 1
    property real playhead: 0
    property real framesPerSecond: 30
    property int timeTickCount: 4
    property real plotLeft: theme.scaled(56)
    property bool editable: true
    property var valueAt: null
    property var beginEdit: null
    property var moveKey: null
    property var finishEdit: null
    property var cancelEdit: null
    property var addKey: null
    property var selectKey: null
    property var keyHandler: null
    property var keyFilter: null
    property var dragOwner: null
    readonly property bool dense: height < theme.scaled(420) || width < theme.scaled(520)
    readonly property int channelCount: parameters.length
    signal channelActivated(string parameter)
    signal seekRequested(real seconds)
    signal focusCurveRequested(string parameter)
    signal timeRangeRequested(real start, real end)
    signal contextMenuRequested(string parameter, real seconds, point position)
    signal selectionClearRequested()

    // Keep lane delegates (and their mouse grabs) alive while controller
    // publication replaces the rich channel projections during a live edit.
    onChannelsChanged: {
        const next = channels.map(channel => String(channel.parameter))
        if (JSON.stringify(next) !== JSON.stringify(parameters)) parameters = next
    }
    function channelData(parameter) {
        return channels.find(channel => channel.parameter === parameter) || ({keys: [], bounds: {minimum: 0, maximum: 1}})
    }
    function selected(parameter, seconds) {
        return currentParameter === parameter && selectedSeconds.some(value => Math.abs(Number(value) - seconds) < 0.001)
    }
    function fitChannels() {
        for (const child of laneList.contentItem.children) {
            if (child.frozenBounds !== undefined) child.frozenBounds = null
        }
    }
    function endInteraction() {
        if (dragOwner) dragOwner.endDrag()
    }
    function cancelInteraction() {
        if (!dragOwner || !dragOwner.editing) return false
        dragOwner.cancelDrag()
        return true
    }
    function isContextShortcut(key, modifiers) {
        return key === Qt.Key_Menu || (key === Qt.Key_F10 && (modifiers & Qt.ShiftModifier))
    }
    function openCurrentContextMenu() {
        if (!visible) return
        const contextTarget = targetId
        const index = parameters.indexOf(currentParameter)
        if (index < 0) return
        laneList.positionViewAtIndex(index, ListView.Contain)
        Qt.callLater(function() {
            const lane = laneList.itemAtIndex(index)
            if (!root.visible || root.targetId !== contextTarget || !lane || lane.parameter !== root.currentParameter) return
            const x = Math.max(root.plotLeft, Math.min(lane.width - root.theme.space16, lane.xForTime(root.playhead)))
            root.contextMenuRequested(lane.parameter, root.playhead,
                lane.mapToItem(root, x, lane.height / 2))
        })
    }
    onTargetIdChanged: if (dragOwner) dragOwner.cancelDrag()
    onCurrentParameterChanged: if (dragOwner && dragOwner.parent.parameter !== currentParameter) dragOwner.cancelDrag()
    onVisibleChanged: if (!visible) endInteraction()
    onEditableChanged: if (!editable) endInteraction()
    Component.onDestruction: endInteraction()

    Keys.onShortcutOverride: function(event) {
        event.accepted = isContextShortcut(event.key, event.modifiers) || (keyFilter ? keyFilter(event.key, event.modifiers) : false)
    }
    Keys.onPressed: function(event) {
        if (isContextShortcut(event.key, event.modifiers)) {
            openCurrentContextMenu()
            event.accepted = true
        } else event.accepted = keyHandler ? keyHandler(event.key, event.modifiers) : false
    }

    Label {
        anchors.centerIn: parent
        width: Math.max(1, parent.width - root.theme.space24)
        visible: root.parameters.length === 0
        text: qsTr("Choose channels to compare.")
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: root.theme.textMuted
        font.family: root.theme.fontUi
        font.pixelSize: root.theme.typeCaption
    }
    ListView {
        id: laneList
        objectName: "timelineAnimationChannelList"
        anchors.fill: parent
        clip: true
        spacing: root.theme.space4
        model: root.parameters
        reuseItems: false
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Rectangle {
            id: lane
            required property string modelData
            required property int index
            readonly property string parameter: modelData
            readonly property var laneData: root.channelData(parameter)
            readonly property var keys: laneData.keys || []
            readonly property bool active: root.currentParameter === parameter
            property var frozenBounds: null
            property string contextTargetId: root.targetId
            onContextTargetIdChanged: frozenBounds = null
            readonly property var bounds: {
                if (frozenBounds) return frozenBounds
                if (!keys.length) return laneData.bounds
                let low = Infinity, high = -Infinity
                for (const key of keys) {
                    const value = Number(key.value)
                    low = Math.min(low, value, value + Number(key.bezierInValue || 0), value + Number(key.bezierOutValue || 0))
                    high = Math.max(high, value, value + Number(key.bezierInValue || 0), value + Number(key.bezierOutValue || 0))
                }
                const span = Math.max(high - low, (Number(laneData.bounds.maximum) - Number(laneData.bounds.minimum)) * 0.025, 0.001)
                return {minimum: low - span * 0.15, maximum: high + span * 0.15}
            }
            objectName: "timelineAnimationLane-" + parameter
            width: laneList.width
            height: root.theme.scaled(root.height < root.theme.scaled(170) ? 62 : root.dense ? 78 : 106)
            radius: root.theme.radius4
            color: root.theme.ink950
            border.width: active ? 1 : 0
            border.color: root.theme.alpha(root.theme.selectionAccent, 0.45)
            clip: true

            function xForTime(seconds) {
                return root.plotLeft + (seconds - root.timeStart) / Math.max(0.001, root.timeSpan)
                    * Math.max(1, width - root.plotLeft - root.theme.space16)
            }
            function timeForX(x) {
                const raw = root.timeStart + (x - root.plotLeft) / Math.max(1, width - root.plotLeft - root.theme.space16) * root.timeSpan
                const fps = Math.max(1, root.framesPerSecond)
                return Math.max(0, Math.min(root.duration, Math.round(raw * fps) / fps))
            }
            function yForValue(value) {
                return plot.y + root.theme.space4 + (Number(bounds.maximum) - value)
                    / Math.max(0.0001, Number(bounds.maximum) - Number(bounds.minimum)) * Math.max(1, plot.height - root.theme.space8)
            }
            function valueForY(y) {
                const fraction = Math.max(0, Math.min(1, (y - plot.y - root.theme.space4) / Math.max(1, plot.height - root.theme.space8)))
                return Math.max(Number(laneData.bounds.minimum), Math.min(Number(laneData.bounds.maximum),
                    Number(bounds.maximum) - fraction * (Number(bounds.maximum) - Number(bounds.minimum))))
            }
            function valueText(value) {
                const scaled = value * Number(laneData.scale || 1)
                return scaled.toFixed(Math.abs(scaled) >= 100 ? 0 : 1) + String(laneData.unit || "")
            }
            onKeysChanged: plotCanvas.requestPaint()
            onBoundsChanged: plotCanvas.requestPaint()

            RowLayout {
                id: header
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: root.theme.space4
                spacing: root.theme.space4
                Label {
                    Layout.fillWidth: true
                    text: String(lane.laneData.label || lane.parameter)
                    elide: Text.ElideRight
                    color: lane.active ? root.theme.textStrong : root.theme.textMain
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    font.weight: lane.active ? Font.DemiBold : Font.Normal
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.channelActivated(lane.parameter)
                    }
                }
                Label {
                    text: lane.laneData.knownValue === false && !lane.keys.length ? "—" : lane.valueText(Number(lane.laneData.value || 0))
                    color: root.theme.textMuted
                    font.family: root.theme.fontNumeric
                    font.features: root.theme.numericFontFeatures
                    font.pixelSize: root.theme.typeMicro
                }
                Label {
                    text: lane.keys.length ? lane.keys.length + " ◆" : ""
                    color: root.theme.textFaint
                    font.family: root.theme.fontNumeric
                    font.features: root.theme.numericFontFeatures
                    font.pixelSize: root.theme.typeMicro
                }
                ActionButton {
                    objectName: "timelineAnimationLaneAdd-" + lane.parameter
                    text: qsTr("Add %1 keyframe at playhead").arg(lane.laneData.label || lane.parameter)
                    iconName: "plus"
                    showText: false
                    compact: true
                    tone: "quiet"
                    enabled: root.editable && root.playhead >= 0 && root.playhead < root.duration
                    tooltipText: text
                    onClicked: {
                        root.channelActivated(lane.parameter)
                        if (root.addKey) root.addKey(lane.parameter, Math.round(root.playhead * root.framesPerSecond) / root.framesPerSecond, null)
                    }
                }
                ActionButton {
                    objectName: "timelineAnimationLaneFocus-" + lane.parameter
                    text: qsTr("Open %1 curve").arg(lane.laneData.label || lane.parameter)
                    iconName: "curve"
                    showText: false
                    compact: true
                    tone: "quiet"
                    tooltipText: text
                    onClicked: root.focusCurveRequested(lane.parameter)
                }
            }

            Item {
                id: plot
                anchors.top: header.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottomMargin: root.theme.space4
                Canvas {
                    id: plotCanvas
                    anchors.fill: parent
                    anchors.leftMargin: root.plotLeft
                    anchors.rightMargin: root.theme.space16
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.lineWidth = 1
                        ctx.strokeStyle = root.theme.alpha(root.theme.lineSoft, 0.5)
                        for (let i = 0; i <= root.timeTickCount; ++i) {
                            const x = Math.round(width * i / Math.max(1, root.timeTickCount)) + 0.5
                            ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                        }
                        ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
                        if ((lane.keys.length || lane.laneData.knownValue !== false) && root.valueAt) {
                            ctx.strokeStyle = lane.active ? root.theme.textStrong : root.theme.textMuted
                            ctx.lineWidth = lane.active ? 1.8 : 1.2
                            ctx.beginPath()
                            const samples = Math.max(40, Math.min(240, Math.round(width / 3)))
                            for (let i = 0; i <= samples; ++i) {
                                const seconds = root.timeStart + root.timeSpan * i / samples
                                const value = root.valueAt(lane.parameter, seconds, Number(lane.laneData.value || 0))
                                const y = lane.yForValue(value) - plot.y
                                if (!i) ctx.moveTo(0, y); else ctx.lineTo(width * i / samples, y)
                            }
                            ctx.stroke()
                        }
                        const x = (root.playhead - root.timeStart) / Math.max(0.001, root.timeSpan) * width
                        if (x >= 0 && x <= width) {
                            ctx.strokeStyle = root.theme.alpha(root.theme.selectionAccent, 0.7)
                            ctx.lineWidth = 1
                            ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                        }
                    }
                }
                Connections {
                    target: root
                    function onTimeStartChanged() { plotCanvas.requestPaint() }
                    function onTimeSpanChanged() { plotCanvas.requestPaint() }
                    function onPlayheadChanged() { plotCanvas.requestPaint() }
                    function onChannelsChanged() { plotCanvas.requestPaint() }
                }
                Label {
                    x: root.theme.space4
                    y: 0
                    width: root.plotLeft - root.theme.space12
                    text: lane.valueText(Number(lane.bounds.maximum))
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: root.theme.textFaint
                    font.family: root.theme.fontNumeric
                    font.features: root.theme.numericFontFeatures
                    font.pixelSize: root.theme.typeMicro
                }
                Label {
                    x: root.theme.space4
                    anchors.bottom: parent.bottom
                    width: root.plotLeft - root.theme.space12
                    text: lane.valueText(Number(lane.bounds.minimum))
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    color: root.theme.textFaint
                    font.family: root.theme.fontNumeric
                    font.features: root.theme.numericFontFeatures
                    font.pixelSize: root.theme.typeMicro
                }
            }
            Repeater {
                model: lane.keys
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property real seconds: Number(modelData.seconds)
                    objectName: "timelineAnimationLanePoint-" + lane.parameter + "-" + index
                    visible: seconds >= root.timeStart - 0.0001 && seconds <= root.timeStart + root.timeSpan + 0.0001
                    width: root.selected(lane.parameter, seconds) ? 9 : 7
                    height: width
                    x: lane.xForTime(seconds) - width / 2
                    y: lane.yForValue(Number(modelData.value)) - height / 2
                    rotation: 45
                    radius: 1
                    color: root.selected(lane.parameter, seconds) ? root.theme.textStrong : root.theme.ink950
                    border.width: 1
                    border.color: root.theme.textStrong
                }
            }
            MouseArea {
                id: lanePointer
                objectName: "timelineAnimationLanePointer-" + lane.parameter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: plot.top
                anchors.bottom: parent.bottom
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true
                cursorShape: hitIndex(mouseX, mouseY + y) >= 0 ? Qt.SizeAllCursor : Qt.CrossCursor
                property real pointSeconds: -1
                property real originalSeconds: -1
                property real originalValue: 0
                property point pressPoint: Qt.point(0, 0)
                property bool editing: false
                property bool canceled: false
                property bool ignoredPress: false
                property bool gesturePublished: false
                property string dragTarget: ""
                property real nextSeconds: 0
                property real nextValue: 0
                function selectAll() { if (root.keyHandler) root.keyHandler(Qt.Key_A, Qt.ControlModifier) }
                function deselect() { if (root.keyHandler) root.keyHandler(Qt.Key_Escape, Qt.NoModifier) }
                function insidePlot(px, py) {
                    const right = width - root.theme.space16
                    return right > root.plotLeft && px >= root.plotLeft && px <= right
                        && py >= plot.y && py <= plot.y + plot.height
                }
                function hitIndex(px, py) {
                    if (!insidePlot(px, py)) return -1
                    let hit = -1, distance = root.theme.scaled(12)
                    lane.keys.forEach(function(key, index) {
                        const seconds = Number(key.seconds)
                        if (seconds < root.timeStart || seconds > root.timeStart + root.timeSpan) return
                        const d = Math.hypot(px - lane.xForTime(seconds), py - lane.yForValue(Number(key.value)))
                        if (d < distance) { distance = d; hit = index }
                    })
                    return hit
                }
                function publish() {
                    if (!editing || pointSeconds < 0 || !root.moveKey || dragTarget !== root.targetId || root.currentParameter !== lane.parameter) return
                    const changed = root.moveKey(lane.parameter, pointSeconds, nextSeconds, nextValue)
                    if (changed) pointSeconds = nextSeconds
                    gesturePublished = gesturePublished || changed
                    if (root.selectKey) root.selectKey(lane.parameter, pointSeconds, false)
                    root.seekRequested(pointSeconds)
                }
                function finishDrag(publishPending) {
                    publishTimer.stop()
                    if (editing && publishPending) publish()
                    const wasEditing = editing
                    editing = false
                    root.dragOwner = null
                    pointSeconds = -1
                    if (wasEditing && root.finishEdit) root.finishEdit()
                    gesturePublished = false
                }
                function endDrag() { finishDrag(true) }
                function cancelDrag() {
                    const undoEdit = editing && gesturePublished
                    canceled = true
                    finishDrag(false)
                    if (undoEdit && root.cancelEdit) root.cancelEdit()
                }
                onPressed: function(mouse) {
                    ignoredPress = !insidePlot(mouse.x, mouse.y + y)
                    if (ignoredPress) return
                    forceActiveFocus()
                    canceled = false
                    gesturePublished = false
                    root.channelActivated(lane.parameter)
                    const hit = hitIndex(mouse.x, mouse.y + y)
                    pressPoint = Qt.point(mouse.x, mouse.y)
                    if (hit >= 0) {
                        pointSeconds = Number(lane.keys[hit].seconds)
                        originalSeconds = pointSeconds
                        originalValue = Number(lane.keys[hit].value)
                        if (root.selectKey && (mouse.button !== Qt.RightButton || !root.selected(lane.parameter, pointSeconds)))
                            root.selectKey(lane.parameter, pointSeconds, mouse.button !== Qt.RightButton && (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.MetaModifier)) !== 0)
                    } else {
                        pointSeconds = -1
                        if (mouse.button === Qt.RightButton) root.selectionClearRequested()
                    }
                }
                onPositionChanged: function(mouse) {
                    if (ignoredPress || !(pressedButtons & Qt.LeftButton) || pointSeconds < 0 || !root.editable) return
                    if (!editing && Math.hypot(mouse.x - pressPoint.x, mouse.y - pressPoint.y) < 2) return
                    if (!editing) {
                        lane.frozenBounds = lane.bounds
                        dragTarget = root.targetId
                        editing = root.beginEdit ? root.beginEdit(lane.parameter) : false
                        if (editing) root.dragOwner = lanePointer
                    }
                    if (!editing) return
                    nextSeconds = lane.timeForX(mouse.x)
                    nextValue = lane.valueForY(mouse.y + y)
                    if (mouse.modifiers & Qt.ShiftModifier) {
                        if (Math.abs(mouse.x - pressPoint.x) >= Math.abs(mouse.y - pressPoint.y)) nextValue = originalValue
                        else nextSeconds = originalSeconds
                    }
                    if (!publishTimer.running) publishTimer.start()
                }
                onReleased: function(mouse) {
                    if (ignoredPress) { ignoredPress = false; canceled = false; return }
                    if (canceled) { canceled = false; return }
                    if (mouse.button === Qt.RightButton) {
                        root.contextMenuRequested(lane.parameter, pointSeconds >= 0 ? pointSeconds : lane.timeForX(mouse.x),
                            lanePointer.mapToItem(root, mouse.x, mouse.y))
                        return
                    }
                    if (editing) endDrag()
                    else root.seekRequested(pointSeconds >= 0 ? pointSeconds : lane.timeForX(mouse.x))
                }
                onCanceled: endDrag()
                onDoubleClicked: function(mouse) {
                    if (mouse.button !== Qt.LeftButton || !insidePlot(mouse.x, mouse.y + y)) return
                    root.channelActivated(lane.parameter)
                    if (root.addKey) root.addKey(lane.parameter, lane.timeForX(mouse.x), lane.valueForY(mouse.y + y))
                }
                onWheel: function(wheel) {
                    if (!(wheel.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier))) { wheel.accepted = false; return }
                    const delta = wheel.angleDelta.y || wheel.pixelDelta.y
                    if (wheel.modifiers & Qt.ShiftModifier) {
                        const offset = -delta / 120 * root.timeSpan * 0.1
                        root.timeRangeRequested(root.timeStart + offset, root.timeStart + root.timeSpan + offset)
                    } else {
                        const center = lane.timeForX(wheel.x)
                        const fraction = (center - root.timeStart) / Math.max(0.001, root.timeSpan)
                        const span = root.timeSpan * (delta > 0 ? 0.8 : 1.25)
                        root.timeRangeRequested(center - span * fraction, center + span * (1 - fraction))
                    }
                    wheel.accepted = true
                }
                Timer { id: publishTimer; interval: 16; onTriggered: lanePointer.publish() }
            }
            Component.onDestruction: if (lanePointer.editing) lanePointer.endDrag()
        }
    }
}
