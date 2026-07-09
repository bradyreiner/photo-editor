import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

pragma ComponentBehavior: Bound

Item {
    id: root

    property string title: ""
    property real hue: 0
    property real saturation: 0
    property real luminance: 0
    property color accent: "#39FFAE"
    property real wheelSize: 82
    property real maxSaturation: 100
    property bool ringMode: false
    readonly property int minimumUsefulWidth: Math.ceil(Math.max(root.wheelSize + 14,
                                                                 titleLabel.implicitWidth + 10,
                                                                 valueRow.implicitWidth + 2))
    signal hueEdited(real value)
    signal saturationEdited(real value)
    signal luminanceEdited(real value)

    Theme { id: theme }

    implicitWidth: minimumUsefulWidth
    implicitHeight: titleLabel.implicitHeight + root.wheelSize + valueRow.implicitHeight + (root.ringMode ? 18 : 12)

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function normalizedHue(value) {
        return ((value % 1) + 1) % 1
    }

    function displaySignedNumber(value) {
        const rounded = Math.round(value)
        return (rounded > 0 ? "+" : "") + rounded
    }

    function displayHue(value) {
        return Math.round(normalizedHue(value) * 360) % 360
    }

    function innerValueX() {
        return wheelWrap.width / 2 + (clamp(root.saturation, 0, root.maxSaturation) / root.maxSaturation - 0.5) * innerBounds.width
    }

    function innerValueY() {
        return wheelWrap.height / 2 - clamp(root.luminance, -100, 100) / 200 * innerBounds.height
    }

    function railValueX(value, minimum, maximum, width) {
        return clamp((value - minimum) / Math.max(1, maximum - minimum), 0, 1) * width
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.ringMode ? 7 : 5

        Label {
            id: titleLabel
            Layout.alignment: Qt.AlignHCenter
            text: root.title
            color: root.ringMode ? theme.textStrong : theme.textFaint
            font.family: theme.fontUi
            font.pixelSize: root.ringMode ? 12 : 9
            font.weight: root.ringMode ? Font.Bold : Font.Medium
            font.capitalization: Font.MixedCase
            font.letterSpacing: 0
        }

        Item {
            id: wheelWrap
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.wheelSize
            Layout.preferredHeight: root.wheelSize

            QtObject {
                id: innerBounds
                readonly property real width: Math.max(28, wheelWrap.width * (root.ringMode ? 0.56 : 0.88))
                readonly property real height: Math.max(28, wheelWrap.height * (root.ringMode ? 0.42 : 0.88))
            }

            Canvas {
                id: wheelCanvas
                anchors.fill: parent
                antialiasing: true

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const cx = width / 2
                    const cy = height / 2
                    const radius = Math.min(width, height) * (root.ringMode ? 0.46 : 0.44)
                    const innerRadius = root.ringMode ? radius * 0.86 : 0
                    const segments = 180

                    for (let i = 0; i < segments; ++i) {
                        const start = (Math.PI * 2 * i / segments) - Math.PI / 2
                        const end = (Math.PI * 2 * (i + 1) / segments) - Math.PI / 2
                        ctx.beginPath()
                        if (root.ringMode) {
                            ctx.arc(cx, cy, radius, start, end)
                            ctx.arc(cx, cy, innerRadius, end, start, true)
                            ctx.closePath()
                        } else {
                            ctx.moveTo(cx, cy)
                            ctx.arc(cx, cy, radius, start, end)
                            ctx.closePath()
                        }
                        ctx.fillStyle = Qt.hsla(i / segments, 0.94, root.ringMode ? 0.54 : 0.52, 1.0)
                        ctx.fill()
                    }

                    if (!root.ringMode) {
                        const desat = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius)
                        desat.addColorStop(0, Qt.rgba(0.86, 0.90, 0.90, 0.92))
                        desat.addColorStop(0.44, Qt.rgba(0.18, 0.22, 0.22, 0.38))
                        desat.addColorStop(1, Qt.rgba(0.02, 0.03, 0.03, 0.0))
                        ctx.fillStyle = desat
                        ctx.beginPath()
                        ctx.arc(cx, cy, radius, 0, Math.PI * 2)
                        ctx.fill()
                    }

                    ctx.lineWidth = root.ringMode ? 2 : 1
                    ctx.strokeStyle = theme.alpha(root.accent, root.ringMode ? 0.55 : 0.35)
                    ctx.beginPath()
                    ctx.arc(cx, cy, radius, 0, Math.PI * 2)
                    ctx.stroke()

                    if (root.ringMode) {
                        ctx.lineWidth = 1
                        ctx.strokeStyle = theme.alpha(theme.polar, 0.10)
                        ctx.beginPath()
                        ctx.arc(cx, cy, innerRadius - 2, 0, Math.PI * 2)
                        ctx.stroke()
                    } else {
                        ctx.strokeStyle = theme.alpha(theme.polar, 0.08)
                        ctx.beginPath()
                        ctx.arc(cx, cy, radius * 0.5, 0, Math.PI * 2)
                        ctx.stroke()
                    }

                    if (!root.ringMode) {
                        ctx.strokeStyle = theme.alpha(theme.polar, 0.14)
                        ctx.beginPath()
                        ctx.moveTo(cx - radius, cy)
                        ctx.lineTo(cx + radius, cy)
                        ctx.moveTo(cx, cy - radius)
                        ctx.lineTo(cx, cy + radius)
                        ctx.stroke()
                    }
                }

                Component.onCompleted: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
            }

            Rectangle {
                visible: root.ringMode
                z: 3
                x: wheelWrap.width / 2 - innerBounds.width / 2
                y: wheelWrap.height / 2 - innerBounds.height / 2
                width: innerBounds.width
                height: innerBounds.height
                radius: 5
                color: theme.alpha(theme.ink950, 0.58)
                border.width: 1
                border.color: theme.alpha(theme.polar, 0.10)
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 7

                    Repeater {
                        model: [
                            { label: "Sat", value: root.saturation, minimum: 0, maximum: root.maxSaturation, defaultValue: 0, kind: "saturation" },
                            { label: "Lum", value: root.luminance, minimum: -100, maximum: 100, defaultValue: 0, kind: "luminance" }
                        ]

                        RowLayout {
                            id: railRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 7

                            Label {
                                Layout.preferredWidth: 24
                                text: String(railRow.modelData.label || "")
                                color: theme.textMain
                                font.family: theme.fontUi
                                font.pixelSize: 9
                                font.weight: Font.Bold
                                font.letterSpacing: 0
                            }

                            Item {
                                id: ringInnerRail
                                Layout.fillWidth: true
                                Layout.preferredHeight: 14
                                readonly property real minimumValue: Number(railRow.modelData.minimum || 0)
                                readonly property real maximumValue: Number(railRow.modelData.maximum || 100)
                                readonly property real defaultValue: Number(railRow.modelData.defaultValue || 0)
                                readonly property real railValue: Number(railRow.modelData.value || 0)
                                readonly property real defaultX: root.railValueX(defaultValue, minimumValue, maximumValue, width)
                                readonly property real valueX: root.railValueX(railValue, minimumValue, maximumValue, width)

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 3
                                    radius: 2
                                    color: theme.alpha(theme.polar, 0.18)
                                }

                                Rectangle {
                                    x: Math.min(ringInnerRail.defaultX, ringInnerRail.valueX)
                                    width: Math.max(1, Math.abs(ringInnerRail.valueX - ringInnerRail.defaultX))
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 3
                                    radius: 2
                                    color: root.accent
                                    opacity: Math.abs(ringInnerRail.railValue - ringInnerRail.defaultValue) > 0.5 ? 0.84 : 0
                                }

                                Rectangle {
                                    x: Math.max(0, Math.min(parent.width - width, ringInnerRail.valueX - width / 2))
                                    y: parent.height / 2 - height / 2
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: theme.alpha(theme.polar, 0.94)
                                    border.width: 1
                                    border.color: theme.alpha(root.accent, 0.82)
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.SizeHorCursor
                                    function editFromX(mouseX) {
                                        const value = ringInnerRail.minimumValue +
                                                      root.clamp(mouseX / Math.max(1, width), 0, 1) *
                                                      (ringInnerRail.maximumValue - ringInnerRail.minimumValue)
                                        if (railRow.modelData.kind === "saturation")
                                            root.saturationEdited(value)
                                        else
                                            root.luminanceEdited(value)
                                    }
                                    onPressed: function(mouse) { editFromX(mouse.x) }
                                    onPositionChanged: function(mouse) {
                                        if (pressed)
                                            editFromX(mouse.x)
                                    }
                                    onDoubleClicked: {
                                        if (railRow.modelData.kind === "saturation")
                                            root.saturationEdited(0)
                                        else
                                            root.luminanceEdited(0)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                z: root.ringMode ? 2 : 0
                readonly property real wheelRadius: Math.min(wheelWrap.width, wheelWrap.height) * (root.ringMode ? 0.46 : 0.44)
                readonly property real ringHandleRadius: root.ringMode ? wheelRadius * 0.93 : wheelRadius
                readonly property real angle: root.normalizedHue(root.hue) * Math.PI * 2 - Math.PI / 2
                readonly property real radiusAmount: root.ringMode ? 1.0 : root.clamp(root.saturation, 0, root.maxSaturation) / root.maxSaturation

                x: wheelWrap.width / 2 + Math.cos(angle) * (root.ringMode ? ringHandleRadius : wheelRadius * radiusAmount) - width / 2
                y: wheelWrap.height / 2 + Math.sin(angle) * (root.ringMode ? ringHandleRadius : wheelRadius * radiusAmount) - height / 2
                width: root.ringMode ? 10 : 10
                height: width
                radius: width / 2
                color: root.ringMode ? theme.alpha(theme.polar, 0.96) : theme.alpha(theme.ink950, 0.74)
                border.width: root.ringMode ? 1 : 2
                border.color: root.ringMode ? root.accent : theme.polar
            }

            MouseArea {
                z: 1
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: function(mouse) { updateWheel(mouse.x, mouse.y) }
                onPositionChanged: function(mouse) {
                    if (pressed)
                        updateWheel(mouse.x, mouse.y)
                }
                onDoubleClicked: {
                    root.hueEdited(0)
                    root.saturationEdited(0)
                    root.luminanceEdited(0)
                }

                function updateWheel(mouseX, mouseY) {
                    const dx = mouseX - width / 2
                    const dy = mouseY - height / 2
                    const angle = Math.atan2(dy, dx) + Math.PI / 2
                    const normalizedHue = ((angle / (Math.PI * 2)) + 1) % 1
                    const radius = Math.sqrt(dx * dx + dy * dy) / (Math.min(width, height) * (root.ringMode ? 0.46 : 0.44))

                    if (root.ringMode && radius < 0.80)
                        return

                    const nextHue = normalizedHue
                    const nextSat = root.clamp(radius * root.maxSaturation, 0, root.maxSaturation)
                    if (pressedButtons & Qt.RightButton) {
                        root.hueEdited(root.hue + (nextHue - root.hue) * 0.18)
                        if (!root.ringMode)
                            root.saturationEdited(root.saturation + (nextSat - root.saturation) * 0.18)
                        return
                    }
                    root.hueEdited(nextHue)
                    if (!root.ringMode)
                        root.saturationEdited(nextSat)
                }
            }
        }

        RowLayout {
            id: valueRow
            Layout.fillWidth: true
            spacing: root.ringMode ? 10 : 6
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 28
                text: "H " + root.displayHue(root.hue) + "\u00B0"
                color: root.ringMode ? theme.textStrong : theme.textMain
                font.family: theme.fontMono
                font.pixelSize: root.ringMode ? 10 : 8
                font.weight: root.ringMode ? Font.DemiBold : Font.Normal
                font.letterSpacing: 0
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 28
                text: "S " + root.displaySignedNumber(root.saturation)
                color: root.ringMode ? theme.textStrong : theme.textMain
                font.family: theme.fontMono
                font.pixelSize: root.ringMode ? 10 : 8
                font.weight: root.ringMode ? Font.DemiBold : Font.Normal
                font.letterSpacing: 0
            }
            Label {
                visible: root.ringMode
                Layout.fillWidth: true
                Layout.minimumWidth: 28
                text: "L " + root.displaySignedNumber(root.luminance)
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 10
                font.weight: Font.DemiBold
                font.letterSpacing: 0
            }
        }
    }

    onHueChanged: wheelCanvas.requestPaint()
    onSaturationChanged: wheelCanvas.requestPaint()
    onLuminanceChanged: wheelCanvas.requestPaint()
    onRingModeChanged: wheelCanvas.requestPaint()
}
