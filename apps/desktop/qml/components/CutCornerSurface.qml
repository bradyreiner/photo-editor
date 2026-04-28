import QtQuick

Item {
    id: root

    property color fillColor: "transparent"
    property color borderColor: "#ffffff"
    property real borderWidth: 1
    property real cut: 6
    property real borderOpacity: 1
    property bool cutTopLeft: true
    property bool cutTopRight: false
    property bool cutBottomRight: true
    property bool cutBottomLeft: false

    onFillColorChanged: surface.requestPaint()
    onBorderColorChanged: surface.requestPaint()
    onBorderWidthChanged: surface.requestPaint()
    onBorderOpacityChanged: surface.requestPaint()
    onCutChanged: surface.requestPaint()
    onCutTopLeftChanged: surface.requestPaint()
    onCutTopRightChanged: surface.requestPaint()
    onCutBottomRightChanged: surface.requestPaint()
    onCutBottomLeftChanged: surface.requestPaint()
    onWidthChanged: surface.requestPaint()
    onHeightChanged: surface.requestPaint()

    Canvas {
        id: surface
        anchors.fill: parent
        antialiasing: true

        function tracePath(ctx, inset) {
            const c = Math.max(0, Math.min(root.cut, width / 2, height / 2))
            ctx.beginPath()
            ctx.moveTo(inset + (root.cutTopLeft ? c : 0), inset)
            ctx.lineTo(width - inset - (root.cutTopRight ? c : 0), inset)
            if (root.cutTopRight)
                ctx.lineTo(width - inset, inset + c)
            ctx.lineTo(width - inset, height - inset - (root.cutBottomRight ? c : 0))
            if (root.cutBottomRight)
                ctx.lineTo(width - inset - c, height - inset)
            ctx.lineTo(inset + (root.cutBottomLeft ? c : 0), height - inset)
            if (root.cutBottomLeft)
                ctx.lineTo(inset, height - inset - c)
            ctx.lineTo(inset, inset + (root.cutTopLeft ? c : 0))
            if (root.cutTopLeft)
                ctx.lineTo(inset + c, inset)
            ctx.closePath()
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            tracePath(ctx, root.borderWidth / 2)
            ctx.fillStyle = root.fillColor
            ctx.fill()
            if (root.borderWidth > 0) {
                ctx.globalAlpha = root.borderOpacity
                ctx.strokeStyle = root.borderColor
                ctx.lineWidth = root.borderWidth
                ctx.stroke()
                ctx.globalAlpha = 1
            }
        }
    }
}
