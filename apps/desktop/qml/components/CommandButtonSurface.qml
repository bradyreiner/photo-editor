import QtQuick

Item {
    id: root

    property color fillColor: "transparent"
    property color borderColor: "#ffffff"
    property color accentColor: "#29f0c6"
    property real borderWidth: 1
    property real cut: 12
    property real borderOpacity: 1
    property real accentOpacity: 1
    property real accentWidth: 5
    property real topLineOpacity: 0.12
    property real cornerRadius: 8

    onFillColorChanged: surface.requestPaint()
    onBorderColorChanged: surface.requestPaint()
    onAccentColorChanged: surface.requestPaint()
    onBorderWidthChanged: surface.requestPaint()
    onCutChanged: surface.requestPaint()
    onBorderOpacityChanged: surface.requestPaint()
    onAccentOpacityChanged: surface.requestPaint()
    onAccentWidthChanged: surface.requestPaint()
    onTopLineOpacityChanged: surface.requestPaint()
    onCornerRadiusChanged: surface.requestPaint()
    onWidthChanged: surface.requestPaint()
    onHeightChanged: surface.requestPaint()

    Canvas {
        id: surface
        anchors.fill: parent
        antialiasing: true

        function traceFill(ctx, inset, c) {
            const r = Math.max(0, Math.min(root.cornerRadius, width / 2, height / 2))
            if (r > 0) {
                const left = inset
                const top = inset
                const right = width - inset
                const bottom = height - inset
                ctx.beginPath()
                ctx.moveTo(left + r, top)
                ctx.lineTo(right - r, top)
                ctx.quadraticCurveTo(right, top, right, top + r)
                ctx.lineTo(right, bottom - r)
                ctx.quadraticCurveTo(right, bottom, right - r, bottom)
                ctx.lineTo(left + r, bottom)
                ctx.quadraticCurveTo(left, bottom, left, bottom - r)
                ctx.lineTo(left, top + r)
                ctx.quadraticCurveTo(left, top, left + r, top)
                ctx.closePath()
                return
            }
            ctx.beginPath()
            ctx.moveTo(inset, inset)
            ctx.lineTo(width - inset, inset)
            ctx.lineTo(width - inset, height - c - inset)
            ctx.lineTo(width - c - inset, height - inset)
            ctx.lineTo(inset, height - inset)
            ctx.lineTo(inset, inset)
            ctx.closePath()
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()

            if (width <= 0 || height <= 0)
                return

            const inset = Math.max(0.5, root.borderWidth / 2)
            const c = root.cornerRadius > 0 ? 0 : Math.max(0, Math.min(root.cut, width / 2, height / 2))
            const right = width - inset
            const bottom = height - inset
            const left = inset
            const top = inset
            const slashTopY = bottom - c
            const slashBottomX = right - c
            const r = Math.max(0, Math.min(root.cornerRadius, width / 2, height / 2))

            traceFill(ctx, inset, c)
            ctx.fillStyle = root.fillColor
            ctx.fill()

            if (root.borderWidth > 0) {
                ctx.lineWidth = root.borderWidth
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.strokeStyle = root.borderColor
                ctx.globalAlpha = root.borderOpacity

                traceFill(ctx, inset, c)
                ctx.stroke()
            }

            if (root.topLineOpacity > 0) {
                ctx.lineWidth = 1
                ctx.lineCap = "round"
                ctx.strokeStyle = root.accentColor
                ctx.globalAlpha = root.topLineOpacity
                ctx.beginPath()
                ctx.moveTo(left + Math.max(3, r), top + 1)
                ctx.lineTo(Math.max(left + Math.max(3, r), right - Math.max(3, r)), top + 1)
                ctx.stroke()
            }

            if (root.cornerRadius > 0 && root.accentWidth > 0 && root.accentOpacity > 0) {
                ctx.lineWidth = root.accentWidth
                ctx.lineCap = "round"
                ctx.strokeStyle = root.accentColor
                ctx.globalAlpha = root.accentOpacity
                ctx.beginPath()
                ctx.moveTo(left + Math.max(root.accentWidth, r * 0.55), bottom - inset - 2)
                ctx.lineTo(right - Math.max(root.accentWidth, r * 0.55), bottom - inset - 2)
                ctx.stroke()
            } else if (c > 0 && root.accentWidth > 0 && root.accentOpacity > 0) {
                ctx.lineWidth = root.accentWidth
                ctx.lineCap = "butt"
                ctx.lineJoin = "miter"
                ctx.strokeStyle = root.accentColor
                ctx.globalAlpha = root.accentOpacity
                ctx.beginPath()
                ctx.moveTo(right, slashTopY)
                ctx.lineTo(slashBottomX, bottom)
                ctx.stroke()

                ctx.lineWidth = 1
                ctx.strokeStyle = "#ffffff"
                ctx.globalAlpha = Math.min(0.24, root.accentOpacity * 0.24)
                ctx.beginPath()
                ctx.moveTo(right - root.accentWidth / 2, slashTopY + root.accentWidth / 2)
                ctx.lineTo(slashBottomX + root.accentWidth / 2, bottom - root.accentWidth / 2)
                ctx.stroke()
            }

            ctx.globalAlpha = 1
        }
    }
}
