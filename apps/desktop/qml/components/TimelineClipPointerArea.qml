import QtQuick

MouseArea {
    id: root

    TimelineScaleGeometry { id: geometry }

    property real bodyWidth: parent ? parent.width : 0
    property real minimumHitWidth: 1
    property real contentXOrigin: 0
    property real sequenceStartSeconds: 0
    property real sequenceDurationSeconds: 0
    property real pixelsPerSecond: 1

    signal sequencePressed(var mouse)
    signal sequenceHitRejected(var mouse)

    x: 0
    y: 0
    width: geometry.hitAreaWidth(bodyWidth, minimumHitWidth)
    height: parent ? parent.height : 0

    onPressed: function(mouse) {
        const scale = Number(pixelsPerSecond)
        const start = Number(sequenceStartSeconds)
        const duration = Number(sequenceDurationSeconds)
        if (!isFinite(scale) || scale <= 0 || !isFinite(start) ||
                !isFinite(duration) || duration <= 0) {
            sequenceHitRejected(mouse)
            mouse.accepted = false
            return
        }
        const position = (Number(contentXOrigin) + Number(mouse.x)) / scale
        if (!geometry.containsSequenceTime(position, start, duration)) {
            sequenceHitRejected(mouse)
            mouse.accepted = false
            return
        }
        sequencePressed(mouse)
    }
}
