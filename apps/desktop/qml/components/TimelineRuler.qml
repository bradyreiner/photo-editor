import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root
    objectName: "timelineRuler"
    required property var theme
    required property var tickModel
    required property var cacheRangeModel
    required property real horizontalOffset
    required property real horizontalZoom
    required property real sequenceDuration
    required property real sequenceWidth
    required property real playheadSeconds
    property int idleCursorShape: Qt.ArrowCursor
    property int pressedCursorShape: Qt.ClosedHandCursor

    signal pointerPressed(real x, real y, int button, int modifiers)
    signal pointerMoved(real x, real y)
    signal pointerReleased(real x, real y)
    signal pointerCanceled()

    function sequenceSecondsAt(viewportX) {
        return Math.max(0, Math.min(sequenceDuration,
                                    (horizontalOffset + Number(viewportX || 0))
                                    / Math.max(0.000000000001, horizontalZoom)))
    }

    function viewportXForSeconds(seconds) {
        return Number(seconds || 0) * horizontalZoom - horizontalOffset
    }

    height: 24
    color: theme.alpha(theme.ink950, 0.62)
    clip: true

    Rectangle {
        id: rulerContent
        x: -root.horizontalOffset
        width: Math.max(root.width, root.sequenceWidth)
        height: root.height
        color: "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.theme.hairlineWidth
            color: root.theme.alpha(root.theme.lineSoft, 0.55)
        }

        Repeater {
            model: root.tickModel

            delegate: Item {
                required property var modelData

                readonly property real seconds: Number(modelData.seconds || 0)
                readonly property bool isMajor: modelData.isMajor === true

                x: seconds * root.horizontalZoom
                y: 0
                width: root.theme.hairlineWidth
                height: root.height

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: root.theme.hairlineWidth
                    height: parent.isMajor ? root.height - 4 : root.height / 2 - 2
                    color: parent.isMajor
                           ? root.theme.alpha(root.theme.lineStrong, 0.46)
                           : root.theme.alpha(root.theme.lineSoft, 0.28)
                }

                Label {
                    visible: parent.isMajor
                    anchors.top: parent.top
                    anchors.topMargin: 3
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    text: String(modelData.label || "")
                    color: root.theme.textMuted
                    font.family: root.theme.fontNumeric
                    font.features: root.theme.numericFontFeatures
                    font.pixelSize: root.theme.typeCaption
                }
            }
        }

        Repeater {
            model: root.cacheRangeModel

            delegate: Rectangle {
                required property var modelData

                objectName: "timelineRenderCacheRange"
                x: Number(modelData.startSeconds || 0) * root.horizontalZoom
                y: root.height - 3
                width: Math.max(root.theme.hairlineWidth,
                                Number(modelData.durationSeconds || 0) * root.horizontalZoom)
                height: 3
                visible: String(modelData.status || "missing") !== "missing"
                color: modelData.status === "ready"
                       ? root.theme.accentCool400
                       : modelData.status === "generating"
                         ? root.theme.accentWarm400
                         : root.theme.alpha(root.theme.accentWarm300, 0.48)
            }
        }

        Canvas {
            objectName: "timelineRulerPlayheadThumb"
            x: root.playheadSeconds * root.horizontalZoom - width / 2
            y: 0
            width: 13
            height: root.height
            onXChanged: requestPaint()
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.beginPath()
                context.moveTo(0, 0)
                context.lineTo(13, 0)
                context.lineTo(6.5, root.height - 2)
                context.closePath()
                context.lineWidth = 2
                context.strokeStyle = root.theme.ink950
                context.stroke()
                context.fillStyle = root.theme.focusAccent
                context.fill()
            }
        }
    }

    MouseArea {
        objectName: "timelineRulerPointer"
        anchors.fill: parent
        z: 10
        preventStealing: true
        cursorShape: pressed ? root.pressedCursorShape : root.idleCursorShape
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        onPressed: function(mouse) {
            root.pointerPressed(mouse.x, mouse.y, mouse.button, mouse.modifiers)
        }
        onPositionChanged: function(mouse) {
            if (pressed)
                root.pointerMoved(mouse.x, mouse.y)
        }
        onReleased: function(mouse) {
            root.pointerReleased(mouse.x, mouse.y)
        }
        onCanceled: root.pointerCanceled()
    }
}
