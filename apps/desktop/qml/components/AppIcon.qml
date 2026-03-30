import QtQuick

Canvas {
    id: root

    required property string name
    property color color: "#efede8"
    property real strokeScale: Math.max(1.2, Math.min(width, height) / 10)

    implicitWidth: 14
    implicitHeight: 14
    contextType: "2d"
    antialiasing: true
    smooth: true

    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function stroke(ctx, widthScale) {
        ctx.strokeStyle = root.color
        ctx.fillStyle = root.color
        ctx.lineWidth = root.strokeScale * widthScale
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
    }

    function line(ctx, x1, y1, x2, y2) {
        ctx.beginPath()
        ctx.moveTo(x1, y1)
        ctx.lineTo(x2, y2)
        ctx.stroke()
    }

    function rect(ctx, x, y, w, h, fill) {
        ctx.beginPath()
        ctx.rect(x, y, w, h)
        if (fill)
            ctx.fill()
        else
            ctx.stroke()
    }

    function circle(ctx, x, y, r, fill) {
        ctx.beginPath()
        ctx.arc(x, y, r, 0, Math.PI * 2)
        if (fill)
            ctx.fill()
        else
            ctx.stroke()
    }

    function poly(ctx, points, fill) {
        if (points.length < 2)
            return
        ctx.beginPath()
        ctx.moveTo(points[0][0], points[0][1])
        for (let index = 1; index < points.length; ++index)
            ctx.lineTo(points[index][0], points[index][1])
        ctx.closePath()
        if (fill)
            ctx.fill()
        else
            ctx.stroke()
    }

    onPaint: {
        const ctx = getContext("2d")
        const w = width
        const h = height
        const midX = w / 2
        const midY = h / 2

        ctx.clearRect(0, 0, w, h)
        stroke(ctx, 1)

        switch (root.name) {
        case "plus":
            line(ctx, midX, h * 0.18, midX, h * 0.82)
            line(ctx, w * 0.18, midY, w * 0.82, midY)
            break
        case "save":
            rect(ctx, w * 0.18, h * 0.16, w * 0.64, h * 0.68, false)
            line(ctx, w * 0.28, h * 0.28, w * 0.72, h * 0.28)
            rect(ctx, w * 0.34, h * 0.56, w * 0.32, h * 0.18, false)
            break
        case "chevron-left":
            line(ctx, w * 0.62, h * 0.2, w * 0.36, midY)
            line(ctx, w * 0.36, midY, w * 0.62, h * 0.8)
            break
        case "chevron-right":
            line(ctx, w * 0.38, h * 0.2, w * 0.64, midY)
            line(ctx, w * 0.64, midY, w * 0.38, h * 0.8)
            break
        case "source":
            rect(ctx, w * 0.16, h * 0.24, w * 0.68, h * 0.52, false)
            poly(ctx, [[w * 0.44, h * 0.38], [w * 0.44, h * 0.62], [w * 0.62, midY]], true)
            break
        case "media":
            rect(ctx, w * 0.16, h * 0.2, w * 0.24, h * 0.24, false)
            rect(ctx, w * 0.16, h * 0.56, w * 0.24, h * 0.24, false)
            rect(ctx, w * 0.5, h * 0.2, w * 0.34, h * 0.24, false)
            rect(ctx, w * 0.5, h * 0.56, w * 0.34, h * 0.24, false)
            break
        case "play":
            poly(ctx, [[w * 0.3, h * 0.22], [w * 0.3, h * 0.78], [w * 0.76, midY]], true)
            break
        case "pause":
            rect(ctx, w * 0.26, h * 0.22, w * 0.16, h * 0.56, true)
            rect(ctx, w * 0.58, h * 0.22, w * 0.16, h * 0.56, true)
            break
        case "jump-start":
            rect(ctx, w * 0.18, h * 0.22, w * 0.08, h * 0.56, true)
            poly(ctx, [[w * 0.72, h * 0.22], [w * 0.36, midY], [w * 0.72, h * 0.78]], true)
            break
        case "step-back":
            line(ctx, w * 0.72, midY, w * 0.3, midY)
            line(ctx, w * 0.46, h * 0.3, w * 0.3, midY)
            line(ctx, w * 0.46, h * 0.7, w * 0.3, midY)
            break
        case "step-forward":
            line(ctx, w * 0.28, midY, w * 0.7, midY)
            line(ctx, w * 0.54, h * 0.3, w * 0.7, midY)
            line(ctx, w * 0.54, h * 0.7, w * 0.7, midY)
            break
        case "cut":
            circle(ctx, w * 0.3, h * 0.68, w * 0.1, false)
            circle(ctx, w * 0.58, h * 0.68, w * 0.1, false)
            line(ctx, w * 0.24, h * 0.26, w * 0.68, h * 0.56)
            line(ctx, w * 0.68, h * 0.26, w * 0.36, h * 0.56)
            break
        case "insert":
            line(ctx, midX, h * 0.18, midX, h * 0.62)
            poly(ctx, [[w * 0.34, h * 0.5], [midX, h * 0.74], [w * 0.66, h * 0.5]], true)
            line(ctx, w * 0.22, h * 0.82, w * 0.78, h * 0.82)
            break
        case "overwrite":
            rect(ctx, w * 0.2, h * 0.22, w * 0.6, h * 0.56, false)
            rect(ctx, w * 0.32, h * 0.34, w * 0.48, h * 0.2, true)
            break
        case "move-back":
            line(ctx, w * 0.72, midY, w * 0.3, midY)
            line(ctx, w * 0.46, h * 0.28, w * 0.3, midY)
            line(ctx, w * 0.46, h * 0.72, w * 0.3, midY)
            line(ctx, w * 0.76, h * 0.24, w * 0.76, h * 0.76)
            break
        case "move-forward":
            line(ctx, w * 0.28, midY, w * 0.7, midY)
            line(ctx, w * 0.54, h * 0.28, w * 0.7, midY)
            line(ctx, w * 0.54, h * 0.72, w * 0.7, midY)
            line(ctx, w * 0.24, h * 0.24, w * 0.24, h * 0.76)
            break
        case "delete":
            line(ctx, w * 0.3, h * 0.28, w * 0.7, h * 0.28)
            rect(ctx, w * 0.32, h * 0.32, w * 0.36, h * 0.44, false)
            line(ctx, w * 0.42, h * 0.4, w * 0.42, h * 0.68)
            line(ctx, w * 0.58, h * 0.4, w * 0.58, h * 0.68)
            break
        case "effects":
            line(ctx, midX, h * 0.14, midX, h * 0.86)
            line(ctx, w * 0.14, midY, w * 0.86, midY)
            line(ctx, w * 0.24, h * 0.24, w * 0.76, h * 0.76)
            line(ctx, w * 0.76, h * 0.24, w * 0.24, h * 0.76)
            break
        case "transition":
            poly(ctx, [[w * 0.16, h * 0.24], [w * 0.48, midY], [w * 0.16, h * 0.76]], false)
            poly(ctx, [[w * 0.84, h * 0.24], [w * 0.52, midY], [w * 0.84, h * 0.76]], false)
            break
        case "motion":
            line(ctx, w * 0.18, h * 0.34, w * 0.18, h * 0.18)
            line(ctx, w * 0.18, h * 0.18, w * 0.34, h * 0.18)
            line(ctx, w * 0.66, h * 0.18, w * 0.82, h * 0.18)
            line(ctx, w * 0.82, h * 0.18, w * 0.82, h * 0.34)
            line(ctx, w * 0.18, h * 0.66, w * 0.18, h * 0.82)
            line(ctx, w * 0.18, h * 0.82, w * 0.34, h * 0.82)
            line(ctx, w * 0.66, h * 0.82, w * 0.82, h * 0.82)
            line(ctx, w * 0.82, h * 0.66, w * 0.82, h * 0.82)
            break
        case "audio":
            line(ctx, w * 0.18, midY, w * 0.28, midY)
            line(ctx, w * 0.28, midY, w * 0.38, h * 0.34)
            line(ctx, w * 0.38, h * 0.34, w * 0.5, h * 0.66)
            line(ctx, w * 0.5, h * 0.66, w * 0.62, h * 0.3)
            line(ctx, w * 0.62, h * 0.3, w * 0.74, midY)
            line(ctx, w * 0.74, midY, w * 0.82, midY)
            break
        case "select":
            poly(ctx, [[w * 0.24, h * 0.18], [w * 0.24, h * 0.82], [w * 0.46, h * 0.62], [w * 0.62, h * 0.84], [w * 0.74, h * 0.76], [w * 0.58, h * 0.54], [w * 0.82, h * 0.54]], true)
            break
        case "smart-select":
            poly(ctx, [[w * 0.22, h * 0.16], [w * 0.22, h * 0.82], [w * 0.44, h * 0.60], [w * 0.60, h * 0.84], [w * 0.72, h * 0.76], [w * 0.56, h * 0.52], [w * 0.80, h * 0.52]], true)
            line(ctx, w * 0.62, h * 0.18, w * 0.62, h * 0.34)
            line(ctx, w * 0.54, h * 0.26, w * 0.70, h * 0.26)
            break
        case "lasso":
            ctx.beginPath()
            ctx.moveTo(w * 0.28, h * 0.32)
            ctx.bezierCurveTo(w * 0.16, h * 0.16, w * 0.56, h * 0.10, w * 0.72, h * 0.28)
            ctx.bezierCurveTo(w * 0.88, h * 0.46, w * 0.76, h * 0.74, w * 0.52, h * 0.78)
            ctx.bezierCurveTo(w * 0.26, h * 0.84, w * 0.14, h * 0.52, w * 0.28, h * 0.32)
            ctx.stroke()
            line(ctx, w * 0.46, h * 0.78, w * 0.40, h * 0.92)
            circle(ctx, w * 0.38, h * 0.96, w * 0.04, true)
            break
        case "polygon-tool":
            poly(ctx, [[w * 0.24, h * 0.28], [w * 0.72, h * 0.22], [w * 0.80, h * 0.66], [w * 0.40, h * 0.80]], false)
            circle(ctx, w * 0.24, h * 0.28, w * 0.05, true)
            circle(ctx, w * 0.72, h * 0.22, w * 0.05, true)
            circle(ctx, w * 0.80, h * 0.66, w * 0.05, true)
            circle(ctx, w * 0.40, h * 0.80, w * 0.05, true)
            break
        case "brush-tool":
            poly(ctx, [[w * 0.24, h * 0.72], [w * 0.52, h * 0.44], [w * 0.76, h * 0.68], [w * 0.48, h * 0.90]], false)
            line(ctx, w * 0.52, h * 0.44, w * 0.68, h * 0.28)
            line(ctx, w * 0.68, h * 0.28, w * 0.84, h * 0.44)
            break
        case "brush-subtract":
            poly(ctx, [[w * 0.24, h * 0.72], [w * 0.52, h * 0.44], [w * 0.76, h * 0.68], [w * 0.48, h * 0.90]], false)
            line(ctx, w * 0.52, h * 0.44, w * 0.68, h * 0.28)
            line(ctx, w * 0.68, h * 0.28, w * 0.84, h * 0.44)
            line(ctx, w * 0.18, h * 0.22, w * 0.42, h * 0.22)
            break
        case "erase-tool":
            poly(ctx, [[w * 0.24, h * 0.62], [w * 0.48, h * 0.34], [w * 0.78, h * 0.64], [w * 0.54, h * 0.88]], false)
            line(ctx, w * 0.34, h * 0.76, w * 0.62, h * 0.48)
            break
        case "invert":
            circle(ctx, midX, midY, w * 0.28, false)
            ctx.beginPath()
            ctx.moveTo(midX, h * 0.22)
            ctx.bezierCurveTo(w * 0.34, h * 0.34, w * 0.34, h * 0.66, midX, h * 0.78)
            ctx.lineTo(midX, h * 0.22)
            ctx.fill()
            break
        case "slip":
            line(ctx, w * 0.24, midY, w * 0.76, midY)
            line(ctx, w * 0.24, midY, w * 0.4, h * 0.34)
            line(ctx, w * 0.24, midY, w * 0.4, h * 0.66)
            line(ctx, w * 0.76, midY, w * 0.6, h * 0.34)
            line(ctx, w * 0.76, midY, w * 0.6, h * 0.66)
            break
        case "roll":
            line(ctx, w * 0.22, h * 0.22, w * 0.22, h * 0.78)
            line(ctx, w * 0.78, h * 0.22, w * 0.78, h * 0.78)
            line(ctx, w * 0.34, midY, w * 0.66, midY)
            line(ctx, w * 0.34, midY, w * 0.46, h * 0.38)
            line(ctx, w * 0.34, midY, w * 0.46, h * 0.62)
            line(ctx, w * 0.66, midY, w * 0.54, h * 0.38)
            line(ctx, w * 0.66, midY, w * 0.54, h * 0.62)
            break
        case "link":
            circle(ctx, w * 0.38, midY, w * 0.18, false)
            circle(ctx, w * 0.62, midY, w * 0.18, false)
            line(ctx, w * 0.46, midY, w * 0.54, midY)
            break
        case "snap":
            circle(ctx, midX, midY, w * 0.24, false)
            line(ctx, midX, h * 0.1, midX, h * 0.28)
            line(ctx, midX, h * 0.72, midX, h * 0.9)
            line(ctx, w * 0.1, midY, w * 0.28, midY)
            line(ctx, w * 0.72, midY, w * 0.9, midY)
            break
        case "ripple":
            ctx.beginPath()
            ctx.moveTo(w * 0.16, midY)
            ctx.bezierCurveTo(w * 0.26, h * 0.26, w * 0.38, h * 0.74, w * 0.5, midY)
            ctx.bezierCurveTo(w * 0.62, h * 0.26, w * 0.74, h * 0.74, w * 0.84, midY)
            ctx.stroke()
            break
        case "library":
            rect(ctx, w * 0.2, h * 0.24, w * 0.18, h * 0.52, false)
            rect(ctx, w * 0.42, h * 0.18, w * 0.18, h * 0.58, false)
            rect(ctx, w * 0.64, h * 0.28, w * 0.16, h * 0.48, false)
            break
        case "edit":
            line(ctx, w * 0.24, h * 0.76, w * 0.76, h * 0.24)
            line(ctx, w * 0.58, h * 0.2, w * 0.8, h * 0.42)
            line(ctx, w * 0.2, h * 0.58, w * 0.42, h * 0.8)
            break
        case "carousel":
            rect(ctx, w * 0.14, h * 0.28, w * 0.28, h * 0.44, false)
            rect(ctx, w * 0.36, h * 0.22, w * 0.28, h * 0.52, false)
            rect(ctx, w * 0.58, h * 0.16, w * 0.28, h * 0.6, false)
            break
        case "timeline":
            line(ctx, w * 0.14, h * 0.28, w * 0.86, h * 0.28)
            line(ctx, w * 0.2, h * 0.44, w * 0.8, h * 0.44)
            line(ctx, w * 0.2, h * 0.62, w * 0.8, h * 0.62)
            rect(ctx, w * 0.3, h * 0.38, w * 0.18, h * 0.12, true)
            rect(ctx, w * 0.52, h * 0.56, w * 0.2, h * 0.12, true)
            break
        case "export":
            line(ctx, midX, h * 0.16, midX, h * 0.62)
            poly(ctx, [[w * 0.34, h * 0.5], [midX, h * 0.76], [w * 0.66, h * 0.5]], true)
            rect(ctx, w * 0.22, h * 0.76, w * 0.56, h * 0.08, true)
            break
        default:
            line(ctx, w * 0.2, midY, w * 0.8, midY)
            break
        }
    }
}
