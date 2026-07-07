import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
            font.weight: root.ringMode ? Font.Black : Font.Medium
            font.capitalization: root.ringMode ? Font.AllUppercase : Font.MixedCase
            font.letterSpacing: 0
        }

        Item {
            id: wheelWrap
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.wheelSize
            Layout.preferredHeight: root.wheelSize

            QtObject {
                id: innerBounds
                readonly property real width: Math.max(28, wheelWrap.width * (root.ringMode ? 0.43 : 0.88))
                readonly property real height: Math.max(28, wheelWrap.height * (root.ringMode ? 0.43 : 0.88))
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
                    const innerRadius = root.ringMode ? radius * 0.72 : 0
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
                        ctx.strokeStyle = theme.alpha(theme.polar, 0.16)
                        ctx.beginPath()
                        ctx.arc(cx, cy, innerRadius - 3, 0, Math.PI * 2)
                        ctx.stroke()
                    } else {
                        ctx.strokeStyle = theme.alpha(theme.polar, 0.08)
                        ctx.beginPath()
                        ctx.arc(cx, cy, radius * 0.5, 0, Math.PI * 2)
                        ctx.stroke()
                    }

                    ctx.strokeStyle = theme.alpha(theme.polar, root.ringMode ? 0.18 : 0.14)
                    ctx.beginPath()
                    ctx.moveTo(cx - (root.ringMode ? innerBounds.width / 2 : radius), cy)
                    ctx.lineTo(cx + (root.ringMode ? innerBounds.width / 2 : radius), cy)
                    ctx.moveTo(cx, cy - (root.ringMode ? innerBounds.height / 2 : radius))
                    ctx.lineTo(cx, cy + (root.ringMode ? innerBounds.height / 2 : radius))
                    ctx.stroke()
                }

                Component.onCompleted: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
            }

            Rectangle {
                visible: root.ringMode
                x: wheelWrap.width / 2 - innerBounds.width / 2
                y: wheelWrap.height / 2 - innerBounds.height / 2
                width: innerBounds.width
                height: innerBounds.height
                radius: 3
                color: theme.alpha(theme.ink950, 0.42)
                border.width: 1
                border.color: theme.alpha(theme.polar, 0.14)
            }

            Rectangle {
                visible: root.ringMode
                x: wheelWrap.width / 2 - innerBounds.width / 2
                y: root.innerValueY() - 1
                width: innerBounds.width
                height: 2
                radius: 1
                color: theme.alpha(root.accent, 0.56)
            }

            Rectangle {
                visible: root.ringMode
                x: root.innerValueX() - 1
                y: wheelWrap.height / 2 - innerBounds.height / 2
                width: 2
                height: innerBounds.height
                radius: 1
                color: theme.alpha(root.accent, 0.50)
            }

            Rectangle {
                readonly property real wheelRadius: Math.min(wheelWrap.width, wheelWrap.height) * (root.ringMode ? 0.46 : 0.44)
                readonly property real angle: root.normalizedHue(root.hue) * Math.PI * 2 - Math.PI / 2
                readonly property real radiusAmount: root.ringMode ? 1.0 : root.clamp(root.saturation, 0, root.maxSaturation) / root.maxSaturation

                x: wheelWrap.width / 2 + Math.cos(angle) * wheelRadius * radiusAmount - width / 2
                y: wheelWrap.height / 2 + Math.sin(angle) * wheelRadius * radiusAmount - height / 2
                width: root.ringMode ? 14 : 10
                height: width
                radius: width / 2
                color: theme.alpha(theme.ink950, root.ringMode ? 0.88 : 0.74)
                border.width: 2
                border.color: root.ringMode ? root.accent : theme.polar
            }

            Rectangle {
                visible: root.ringMode
                x: root.innerValueX() - width / 2
                y: root.innerValueY() - height / 2
                width: 16
                height: 16
                radius: 3
                rotation: 45
                color: theme.alpha(theme.polar, 0.92)
                border.width: 2
                border.color: theme.alpha(root.accent, 0.92)

                Rectangle {
                    anchors.centerIn: parent
                    width: 4
                    height: 4
                    radius: 2
                    color: theme.ink950
                }
            }

            MouseArea {
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

                    if (root.ringMode && radius < 0.72) {
                        const nextSat = root.clamp(((mouseX - (width / 2 - innerBounds.width / 2)) / innerBounds.width) * root.maxSaturation,
                                                   0,
                                                   root.maxSaturation)
                        const nextLum = root.clamp(((height / 2 - mouseY) / (innerBounds.height / 2)) * 100, -100, 100)
                        root.saturationEdited(nextSat)
                        root.luminanceEdited(nextLum)
                        return
                    }

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
