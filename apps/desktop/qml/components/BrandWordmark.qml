import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property color accent: theme.vectorGreen
    property color foreground: theme.textStrong
    property bool compact: false
    property bool showMark: true

    Theme { id: theme }

    implicitWidth: compact ? 174 : 212
    implicitHeight: compact ? 30 : 36

    onAccentChanged: markCanvas.requestPaint()
    onForegroundChanged: markCanvas.requestPaint()

    RowLayout {
        anchors.fill: parent
        spacing: compact ? 10 : 12

        Canvas {
            id: markCanvas

            visible: root.showMark
            Layout.preferredWidth: compact ? 24 : 30
            Layout.preferredHeight: compact ? 24 : 30
            Layout.alignment: Qt.AlignVCenter
            antialiasing: true
            contextType: "2d"

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                const w = width
                const h = height
                ctx.clearRect(0, 0, w, h)
                const inset = w * 0.03
                const radius = w * 0.12

                function roundedRect(x, y, rw, rh, rr) {
                    ctx.beginPath()
                    ctx.moveTo(x + rr, y)
                    ctx.lineTo(x + rw - rr, y)
                    ctx.quadraticCurveTo(x + rw, y, x + rw, y + rr)
                    ctx.lineTo(x + rw, y + rh - rr)
                    ctx.quadraticCurveTo(x + rw, y + rh, x + rw - rr, y + rh)
                    ctx.lineTo(x + rr, y + rh)
                    ctx.quadraticCurveTo(x, y + rh, x, y + rh - rr)
                    ctx.lineTo(x, y + rr)
                    ctx.quadraticCurveTo(x, y, x + rr, y)
                    ctx.closePath()
                }

                roundedRect(inset, inset, w - inset * 2, h - inset * 2, radius)
                ctx.fillStyle = theme.alpha(theme.ink850, 0.96)
                ctx.fill()
                ctx.strokeStyle = theme.alpha(theme.polar, 0.24)
                ctx.lineWidth = 1
                ctx.stroke()

                ctx.lineWidth = Math.max(1.3, w * 0.062)
                ctx.lineCap = "square"
                ctx.lineJoin = "miter"
                ctx.strokeStyle = root.foreground
                ctx.beginPath()
                ctx.moveTo(w * 0.22, h * 0.31)
                ctx.lineTo(w * 0.22, h * 0.20)
                ctx.lineTo(w * 0.39, h * 0.20)
                ctx.moveTo(w * 0.61, h * 0.20)
                ctx.lineTo(w * 0.78, h * 0.20)
                ctx.lineTo(w * 0.78, h * 0.36)
                ctx.moveTo(w * 0.78, h * 0.64)
                ctx.lineTo(w * 0.78, h * 0.80)
                ctx.lineTo(w * 0.61, h * 0.80)
                ctx.moveTo(w * 0.39, h * 0.80)
                ctx.lineTo(w * 0.22, h * 0.80)
                ctx.lineTo(w * 0.22, h * 0.69)
                ctx.stroke()
                ctx.fillStyle = root.foreground
                ctx.beginPath()
                ctx.arc(w * 0.50, h * 0.50, Math.max(0.9, w * 0.022), 0, Math.PI * 2)
                ctx.fill()
                ctx.lineWidth = Math.max(2.4, w * 0.092)
                ctx.lineCap = "butt"
                ctx.strokeStyle = root.accent
                ctx.beginPath()
                ctx.moveTo(w * 0.59, h * 0.78)
                ctx.lineTo(w * 0.76, h * 0.56)
                ctx.stroke()
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: compact ? 6 : 8

            Text {
                text: "VIREL"
                color: root.foreground
                font.family: theme.fontDisplay
                font.pixelSize: compact ? 13 : 15
                font.weight: Font.Bold
                font.letterSpacing: compact ? 4.0 : 5.0
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                text: "/"
                color: root.accent
                font.family: theme.fontDisplay
                font.pixelSize: compact ? 13 : 15
                font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                text: "STUDIO"
                color: theme.textMain
                font.family: theme.fontDisplay
                font.pixelSize: compact ? 10 : 11
                font.weight: Font.Normal
                font.letterSpacing: compact ? 2.2 : 2.8
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
