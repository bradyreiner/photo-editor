pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    required property var theme
    property var channels: []
    property var channelParameters: []
    property var channelsByParameter: ({})
    property real duration: 1
    property real playhead: -1
    property real framesPerSecond: 30
    property string selectedParameter: ""
    property real selectedKeyframeSeconds: -1
    property bool editable: true
    property int rowHeight: theme.scaled(34)
    property real rulerHeight: theme.controlHeightDense
    readonly property real implicitContentHeight: rulerHeight + channels.length * rowHeight

    signal channelActivated(string parameter)
    signal seekRequested(real localSeconds)
    signal seekGestureStarted()
    signal seekGestureFinished()
    signal keyframeMoveStarted()
    signal keyframeMoveRequested(string parameter, real fromLocalSeconds, real toLocalSeconds)
    signal keyframeMoveFinished()

    onSelectedParameterChanged: selectedKeyframeSeconds = -1
    onChannelsChanged: refreshChannelIndex()
    Component.onCompleted: refreshChannelIndex()

    implicitWidth: theme.scaled(150)
    implicitHeight: implicitContentHeight
    clip: true
    activeFocusOnTab: true
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Clip keyframe lanes")
    Accessible.description: qsTr("A clip-local time ruler and aligned keyframe lanes. Drag the ruler or blank lane to seek, or drag a keyframe diamond to retime it.")

    function clampedDuration() {
        return Math.max(1 / Math.max(1, framesPerSecond), Number(duration || 0))
    }

    function timeAtX(x, width) {
        const inset = Math.min(theme.space6, Math.max(0, width * 0.08))
        const usable = Math.max(1, width - inset * 2)
        const ratio = Math.max(0, Math.min(1, (Number(x) - inset) / usable))
        const fps = Math.max(1, Number(framesPerSecond || 30))
        return Math.max(0, Math.min(clampedDuration(), Math.round(ratio * clampedDuration() * fps) / fps))
    }

    function xAtTime(seconds, width) {
        const inset = Math.min(theme.space6, Math.max(0, width * 0.08))
        return inset + Math.max(0, Math.min(1, Number(seconds) / clampedDuration()))
                * Math.max(1, width - inset * 2)
    }

    function seekFromKeyboard(frameDelta) {
        const fps = Math.max(1, Number(framesPerSecond || 30))
        const next = Math.max(0, Math.min(clampedDuration(), Number(playhead || 0) + frameDelta / fps))
        seekGestureStarted()
        seekRequested(next)
        seekGestureFinished()
    }

    function refreshChannelIndex() {
        const source = channels || []
        const parameters = []
        const byParameter = Object.create(null)
        for (let index = 0; index < source.length; ++index) {
            const channel = source[index] || ({})
            const parameter = String(channel.parameter || "")
            if (parameter.length === 0)
                continue
            parameters.push(parameter)
            byParameter[parameter] = channel
        }
        const sameParameters = parameters.length === channelParameters.length
                && parameters.every((parameter, index) => parameter === channelParameters[index])
        channelsByParameter = byParameter
        if (!sameParameters)
            channelParameters = parameters
    }

    Item {
        id: ruler
        objectName: "timelineInspectorMiniRuler"
        width: root.width
        height: root.rulerHeight
        clip: true

        Rectangle {
            anchors.fill: parent
            color: root.theme.ink950
            border.width: 0
        }

        Repeater {
            model: 5
            delegate: Item {
                required property int index
                readonly property real seconds: root.clampedDuration() * index / 4
                readonly property real markerX: root.xAtTime(seconds, ruler.width)
                x: markerX
                width: 1
                height: ruler.height

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 1
                    height: parent.height
                    color: root.theme.alpha(root.theme.lineSoft, 0.28)
                }
                Label {
                    anchors.top: parent.top
                    anchors.topMargin: root.theme.scaled(1)
                    anchors.horizontalCenter: index === 0 ? undefined : parent.horizontalCenter
                    x: index === 0 ? 0 : index === 4 ? -implicitWidth : -implicitWidth / 2
                    text: seconds.toFixed(root.clampedDuration() < 10 ? 1 : 0) + "s"
                    color: root.theme.textFaint
                    font.family: root.theme.fontNumeric
                    font.features: root.theme.numericFontFeatures
                    font.pixelSize: root.theme.typeMicro
                    Accessible.ignored: true
                }
            }
        }

        Rectangle {
            objectName: "timelineInspectorMiniRulerPlayhead"
            visible: root.playhead >= 0 && root.playhead <= root.clampedDuration()
            x: root.xAtTime(root.playhead, ruler.width) - width / 2
            y: 0
            width: root.theme.scaled(2)
            height: parent.height
            color: root.theme.focusAccent
            z: 2
        }

        MouseArea {
            objectName: "timelineInspectorMiniRulerPointer"
            anchors.fill: parent
            enabled: root.editable && root.duration > 0
            acceptedButtons: Qt.LeftButton
            preventStealing: true
            cursorShape: Qt.PointingHandCursor
            onPressed: function(mouse) {
                root.forceActiveFocus()
                root.seekGestureStarted()
                root.seekRequested(root.timeAtX(mouse.x, width))
            }
            onPositionChanged: function(mouse) {
                if (pressed)
                    root.seekRequested(root.timeAtX(mouse.x, width))
            }
            onReleased: root.seekGestureFinished()
            onCanceled: root.seekGestureFinished()
        }
    }

    Repeater {
        id: laneRepeater
        objectName: "timelineInspectorMiniLaneRepeater"
        model: root.channelParameters
        delegate: Item {
            id: lane
            required property int index
            required property var modelData
            readonly property string parameter: String(modelData || "")
            readonly property var channel: root.channelsByParameter[parameter] || ({})
            readonly property var keys: channel.keys || []
            readonly property bool selected: root.selectedParameter === parameter
            y: root.rulerHeight + index * root.rowHeight
            width: root.width
            height: root.rowHeight
            objectName: "timelineInspectorMiniLane-" + parameter
            Accessible.role: Accessible.ListItem
            Accessible.name: String(channel.label || parameter)
            Accessible.description: qsTr("%1 keyframes; use the selected channel previous, add, and next controls above.")
                                     .arg(keys.length)
            onKeysChanged: keyframeCanvas.requestPaint()

            Rectangle {
                anchors.fill: parent
                color: lane.selected ? root.theme.alpha(root.theme.selectionAccent, 0.08)
                                     : (lane.index % 2 === 0
                                        ? root.theme.alpha(root.theme.glassLift, 0.07)
                                        : "transparent")
                border.width: 0
                border.color: root.theme.alpha(root.theme.selectionAccent, 0.4)
            }

            Rectangle {
                x: root.theme.space6
                width: Math.max(1, parent.width - root.theme.space12)
                height: 1
                anchors.verticalCenter: parent.verticalCenter
                color: root.theme.alpha(root.theme.lineStrong, 0.18)
            }

            Canvas {
                id: keyframeCanvas
                objectName: "timelineInspectorMiniKeyframeCanvas-" + lane.parameter
                anchors.fill: parent
                renderStrategy: Canvas.Cooperative
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const size = root.theme.scaled(4)
                    const half = size / 2
                    ctx.lineWidth = 1
                    let previousMarkerX = -Infinity
                    for (let index = 0; index < lane.keys.length; ++index) {
                        const seconds = Number(lane.keys[index].seconds || 0)
                        if (seconds < 0 || seconds > root.clampedDuration())
                            continue
                        const moving = lanePointer.draggingKeyframe
                                && Math.abs(seconds - lanePointer.originalKeyframeSeconds)
                                   < 0.5 / Math.max(1, root.framesPerSecond)
                        const drawSeconds = moving
                                ? lanePointer.draftKeyframeSeconds : seconds
                        const x = root.xAtTime(drawSeconds, width)
                        const y = height / 2
                        const current = lane.selected
                                && Math.abs(drawSeconds - root.selectedKeyframeSeconds)
                                   <= 0.5 / Math.max(1, root.framesPerSecond)
                        // Dense, frame-baked motion must remain readable at
                        // overview scale. All keys stay available to hit tests;
                        // selected/moving keys and both endpoints always draw.
                        if (!current && !moving && index !== lane.keys.length - 1
                                && x - previousMarkerX < root.theme.scaled(6))
                            continue
                        if (!moving) previousMarkerX = x
                        ctx.beginPath()
                        ctx.moveTo(x, y - half)
                        ctx.lineTo(x + half, y)
                        ctx.lineTo(x, y + half)
                        ctx.lineTo(x - half, y)
                        ctx.closePath()
                        ctx.fillStyle = current ? root.theme.textStrong : root.theme.ink950
                        ctx.fill()
                        ctx.strokeStyle = root.theme.alpha(root.theme.focusAccent, 0.95)
                        ctx.stroke()
                    }
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Connections {
                    target: root
                    function onChannelsChanged() { keyframeCanvas.requestPaint() }
                    function onSelectedParameterChanged() { keyframeCanvas.requestPaint() }
                    function onSelectedKeyframeSecondsChanged() { keyframeCanvas.requestPaint() }
                }
            }

            Rectangle {
                objectName: "timelineInspectorMiniLanePlayhead-" + lane.parameter
                visible: root.playhead >= 0 && root.playhead <= root.clampedDuration()
                x: root.xAtTime(root.playhead, lane.width) - width / 2
                y: root.theme.space2
                width: root.theme.scaled(2)
                height: Math.max(1, parent.height - root.theme.space4)
                color: root.theme.focusAccent
                z: 2
            }

            MouseArea {
                id: lanePointer
                objectName: "timelineInspectorMiniLanePointer-" + lane.parameter
                anchors.fill: parent
                enabled: root.editable && root.duration > 0
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                cursorShape: Qt.CrossCursor
                property var candidateKeyframe: null
                property real originalKeyframeSeconds: -1
                property real draftKeyframeSeconds: -1
                property real pressX: 0
                property bool seeking: false
                property bool draggingKeyframe: false
                function nearestKey(seconds) {
                    const x = root.xAtTime(seconds, width)
                    let best = null
                    let distance = root.theme.scaled(7)
                    for (let index = 0; index < lane.keys.length; ++index) {
                        const point = lane.keys[index]
                        const keyDistance = Math.abs(root.xAtTime(Number(point.seconds || 0), width) - x)
                        if (keyDistance <= distance) {
                            best = point
                            distance = keyDistance
                        }
                    }
                    return best
                }
                function clearPointerState() {
                    candidateKeyframe = null
                    originalKeyframeSeconds = -1
                    draftKeyframeSeconds = -1
                    seeking = false
                    draggingKeyframe = false
                }
                function finishKeyframeMove() {
                    if (Math.abs(draftKeyframeSeconds - originalKeyframeSeconds)
                            >= 0.5 / Math.max(1, root.framesPerSecond)) {
                        root.keyframeMoveRequested(
                                    lane.parameter, originalKeyframeSeconds,
                                    draftKeyframeSeconds)
                        root.selectedKeyframeSeconds = draftKeyframeSeconds
                    }
                    root.keyframeMoveFinished()
                    clearPointerState()
                    keyframeCanvas.requestPaint()
                }
                onPressed: function(mouse) {
                    root.forceActiveFocus()
                    pressX = mouse.x
                    candidateKeyframe = nearestKey(root.timeAtX(mouse.x, width))
                    root.channelActivated(lane.parameter)
                    if (candidateKeyframe) {
                        originalKeyframeSeconds = Number(candidateKeyframe.seconds || 0)
                        draftKeyframeSeconds = originalKeyframeSeconds
                        root.selectedKeyframeSeconds = originalKeyframeSeconds
                    } else {
                        root.selectedKeyframeSeconds = -1
                        seeking = true
                        root.seekGestureStarted()
                        root.seekRequested(root.timeAtX(mouse.x, width))
                    }
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    if (candidateKeyframe && !draggingKeyframe
                            && Math.abs(mouse.x - pressX) >= root.theme.scaled(4)) {
                        draggingKeyframe = true
                        root.keyframeMoveStarted()
                    }
                    if (draggingKeyframe) {
                        draftKeyframeSeconds = root.timeAtX(mouse.x, width)
                        root.selectedKeyframeSeconds = draftKeyframeSeconds
                        keyframeCanvas.requestPaint()
                    } else if (seeking) {
                        root.seekRequested(root.timeAtX(mouse.x, width))
                    }
                }
                onReleased: {
                    if (draggingKeyframe) {
                        finishKeyframeMove()
                    } else if (candidateKeyframe) {
                        root.seekGestureStarted()
                        root.seekRequested(originalKeyframeSeconds)
                        root.seekGestureFinished()
                        clearPointerState()
                    } else if (seeking) {
                        root.seekGestureFinished()
                        clearPointerState()
                    }
                }
                onCanceled: {
                    if (draggingKeyframe) {
                        root.selectedKeyframeSeconds = originalKeyframeSeconds
                        root.keyframeMoveFinished()
                        clearPointerState()
                        keyframeCanvas.requestPaint()
                    } else if (seeking) {
                        root.seekGestureFinished()
                        clearPointerState()
                    } else {
                        clearPointerState()
                    }
                }
            }
        }
    }

    Keys.onLeftPressed: function(event) {
        seekFromKeyboard(-1)
        event.accepted = true
    }
    Keys.onRightPressed: function(event) {
        seekFromKeyboard(1)
        event.accepted = true
    }

}
