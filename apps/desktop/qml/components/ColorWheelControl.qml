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
    readonly property int minimumUsefulWidth: Math.ceil(Math.max(root.wheelSize + 14,
                                                                 titleLabel.implicitWidth + 10,
                                                                 valueRow.implicitWidth + 2))
    signal hueEdited(real value)
    signal saturationEdited(real value)

    Theme { id: theme }

    implicitWidth: minimumUsefulWidth
    implicitHeight: titleLabel.implicitHeight + root.wheelSize + valueRow.implicitHeight + 12

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

    ColumnLayout {
        anchors.fill: parent
        spacing: 5

        Label {
            id: titleLabel
            Layout.alignment: Qt.AlignHCenter
            text: root.title
            color: theme.textFaint
            font.family: theme.fontUi
            font.pixelSize: 9
            font.letterSpacing: 0
        }

        Item {
            id: wheelWrap
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.wheelSize
            Layout.preferredHeight: root.wheelSize

            Canvas {
                id: wheelCanvas
                anchors.fill: parent
                antialiasing: true

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const cx = width / 2
                    const cy = height / 2
                    const radius = Math.min(width, height) * 0.44
                    const segments = 144
                    for (let i = 0; i < segments; ++i) {
                        const start = (Math.PI * 2 * i / segments) - Math.PI / 2
                        const end = (Math.PI * 2 * (i + 1) / segments) - Math.PI / 2
                        ctx.beginPath()
                        ctx.moveTo(cx, cy)
                        ctx.arc(cx, cy, radius, start, end)
                        ctx.closePath()
                        ctx.fillStyle = Qt.hsla(i / segments, 0.92, 0.52, 1.0)
                        ctx.fill()
                    }

                    const desat = ctx.createRadialGradient(cx, cy, 0, cx, cy, radius)
                    desat.addColorStop(0, Qt.rgba(0.86, 0.90, 0.90, 0.92))
                    desat.addColorStop(0.44, Qt.rgba(0.18, 0.22, 0.22, 0.38))
                    desat.addColorStop(1, Qt.rgba(0.02, 0.03, 0.03, 0.0))
                    ctx.fillStyle = desat
                    ctx.beginPath()
                    ctx.arc(cx, cy, radius, 0, Math.PI * 2)
                    ctx.fill()

                    ctx.lineWidth = 1
                    ctx.strokeStyle = theme.alpha(root.accent, 0.35)
                    ctx.beginPath()
                    ctx.arc(cx, cy, radius, 0, Math.PI * 2)
                    ctx.stroke()
                    ctx.strokeStyle = theme.alpha(theme.polar, 0.08)
                    ctx.beginPath()
                    ctx.arc(cx, cy, radius * 0.5, 0, Math.PI * 2)
                    ctx.stroke()
                    ctx.strokeStyle = theme.alpha(theme.polar, 0.14)
                    ctx.beginPath()
                    ctx.moveTo(cx - radius, cy)
                    ctx.lineTo(cx + radius, cy)
                    ctx.moveTo(cx, cy - radius)
                    ctx.lineTo(cx, cy + radius)
                    ctx.stroke()
                }

                Component.onCompleted: requestPaint()
            }

            Rectangle {
                readonly property real wheelRadius: Math.min(wheelWrap.width, wheelWrap.height) * 0.44
                readonly property real angle: root.normalizedHue(root.hue) * Math.PI * 2 - Math.PI / 2
                readonly property real radiusAmount: root.clamp(root.saturation, 0, root.maxSaturation) / root.maxSaturation

                x: wheelWrap.width / 2 + Math.cos(angle) * wheelRadius * radiusAmount - width / 2
                y: wheelWrap.height / 2 + Math.sin(angle) * wheelRadius * radiusAmount - height / 2
                width: 10
                height: 10
                radius: width / 2
                color: theme.alpha(theme.ink950, 0.74)
                border.width: 2
                border.color: theme.polar
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
                }

                function updateWheel(mouseX, mouseY) {
                    const dx = mouseX - width / 2
                    const dy = mouseY - height / 2
                    const angle = Math.atan2(dy, dx) + Math.PI / 2
                    const normalizedHue = ((angle / (Math.PI * 2)) + 1) % 1
                    const radius = Math.sqrt(dx * dx + dy * dy) / (Math.min(width, height) * 0.44)
                    const nextHue = normalizedHue
                    const nextSat = root.clamp(radius * root.maxSaturation, 0, root.maxSaturation)
                    if (pressedButtons & Qt.RightButton) {
                        root.hueEdited(root.hue + (nextHue - root.hue) * 0.18)
                        root.saturationEdited(root.saturation + (nextSat - root.saturation) * 0.18)
                        return
                    }
                    root.hueEdited(nextHue)
                    root.saturationEdited(nextSat)
                }
            }
        }

        RowLayout {
            id: valueRow
            Layout.fillWidth: true
            spacing: 6
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 28
                text: "H " + root.displayHue(root.hue) + "\u00B0"
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: 8
                font.letterSpacing: 0
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 28
                text: "S " + root.displaySignedNumber(root.saturation)
                color: theme.textMain
                font.family: theme.fontMono
                font.pixelSize: 8
                font.letterSpacing: 0
            }
        }
    }
}
