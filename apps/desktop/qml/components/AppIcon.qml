import QtQuick

Canvas {
    id: root

    required property string name
    property color color: "#efede8"
    property real strokeScale: Math.max(1.15, Math.min(width, height) * 0.085)
    readonly property string iconSystemContract: "app-icon:v2|canvas:16-grid|aspect:square-centered|stroke:optically-scaled|caps:round|joins:round|baseline:center|state-color:caller-semantic"

    implicitWidth: 16
    implicitHeight: 16
    // Acquire the context during paint. Eager contextType creation is queued
    // by Qt and can run after a virtualized icon has left its window.
    antialiasing: true
    smooth: true

    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onStrokeScaleChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onAvailableChanged: if (available) requestPaint()

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
        if (!available || width <= 0 || height <= 0)
            return
        const ctx = getContext("2d")
        if (!ctx)
            return
        const w = Math.min(width, height)
        const h = w
        const midX = w / 2
        const midY = h / 2

        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        if (root.name.length === 0)
            return
        ctx.translate((width - w) / 2, (height - h) / 2)
        stroke(ctx, 1)

        switch (root.name) {
        case "text":
            line(ctx, w * 0.2, h * 0.2, w * 0.8, h * 0.2)
            line(ctx, midX, h * 0.2, midX, h * 0.8)
            line(ctx, w * 0.32, h * 0.8, w * 0.68, h * 0.8)
            break
        case "add":
        case "plus":
            line(ctx, midX, h * 0.18, midX, h * 0.82)
            line(ctx, w * 0.18, midY, w * 0.82, midY)
            break
        case "minus":
            line(ctx, w * 0.18, midY, w * 0.82, midY)
            break
        case "close":
            line(ctx, w * 0.24, h * 0.24, w * 0.76, h * 0.76)
            line(ctx, w * 0.76, h * 0.24, w * 0.24, h * 0.76)
            break
        case "check":
            line(ctx, w * 0.18, midY, w * 0.42, h * 0.74)
            line(ctx, w * 0.42, h * 0.74, w * 0.82, h * 0.28)
            break
        case "swatch":
            circle(ctx, midX, midY, w * 0.30, true)
            break
        case "keyframe":
            poly(ctx, [[midX, h * 0.14], [w * 0.86, midY], [midX, h * 0.86], [w * 0.14, midY]], false)
            break
        case "keyframe-filled":
            poly(ctx, [[midX, h * 0.14], [w * 0.86, midY], [midX, h * 0.86], [w * 0.14, midY]], true)
            break
        case "mark-in":
            line(ctx, w * 0.28, h * 0.18, w * 0.28, h * 0.82)
            line(ctx, w * 0.28, h * 0.24, w * 0.74, h * 0.24)
            break
        case "mark-out":
            line(ctx, w * 0.72, h * 0.18, w * 0.72, h * 0.82)
            line(ctx, w * 0.26, h * 0.24, w * 0.72, h * 0.24)
            break
        case "curve":
            ctx.beginPath()
            ctx.moveTo(w * 0.14, h * 0.78)
            ctx.bezierCurveTo(w * 0.34, h * 0.78, w * 0.36, h * 0.22, w * 0.86, h * 0.22)
            ctx.stroke()
            circle(ctx, w * 0.14, h * 0.78, w * 0.055, true)
            circle(ctx, w * 0.86, h * 0.22, w * 0.055, true)
            break
        case "speed":
            ctx.beginPath()
            ctx.arc(midX, h * 0.66, w * 0.31, Math.PI, 0, false)
            ctx.stroke()
            line(ctx, w * 0.18, h * 0.67, w * 0.82, h * 0.67)
            line(ctx, midX, h * 0.62, w * 0.68, h * 0.38)
            circle(ctx, midX, h * 0.62, w * 0.045, true)
            break
        case "star":
        case "rating":
            poly(ctx, [
                [midX, h * 0.14],
                [w * 0.60, h * 0.40],
                [w * 0.88, h * 0.40],
                [w * 0.66, h * 0.58],
                [w * 0.74, h * 0.84],
                [midX, h * 0.68],
                [w * 0.26, h * 0.84],
                [w * 0.34, h * 0.58],
                [w * 0.12, h * 0.40],
                [w * 0.40, h * 0.40]
            ], root.name === "rating")
            break
        case "save":
            rect(ctx, w * 0.18, h * 0.16, w * 0.64, h * 0.68, false)
            line(ctx, w * 0.28, h * 0.28, w * 0.72, h * 0.28)
            rect(ctx, w * 0.34, h * 0.56, w * 0.32, h * 0.18, false)
            break
        case "chevron-left":
        case "previous":
            line(ctx, w * 0.62, h * 0.2, w * 0.36, midY)
            line(ctx, w * 0.36, midY, w * 0.62, h * 0.8)
            break
        case "chevron-right":
        case "next":
            line(ctx, w * 0.38, h * 0.2, w * 0.64, midY)
            line(ctx, w * 0.64, midY, w * 0.38, h * 0.8)
            break
        case "chevron-up":
            line(ctx, w * 0.2, h * 0.62, midX, h * 0.36)
            line(ctx, midX, h * 0.36, w * 0.8, h * 0.62)
            break
        case "chevron-down":
            line(ctx, w * 0.2, h * 0.38, midX, h * 0.64)
            line(ctx, midX, h * 0.64, w * 0.8, h * 0.38)
            break
        case "settings":
            const teeth = []
            for (let index = 0; index < 24; ++index) {
                const angle = index * Math.PI / 12 - Math.PI / 12
                const radius = w * (index % 4 === 0 || index % 4 === 3 ? 0.36 : 0.29)
                teeth.push([midX + Math.cos(angle) * radius, midY + Math.sin(angle) * radius])
            }
            poly(ctx, teeth, false)
            circle(ctx, midX, midY, w * 0.12, false)
            break
        case "video":
        case "source":
            rect(ctx, w * 0.16, h * 0.24, w * 0.68, h * 0.52, false)
            poly(ctx, [[w * 0.44, h * 0.38], [w * 0.44, h * 0.62], [w * 0.62, midY]], true)
            break
        case "folder":
            line(ctx, w * 0.16, h * 0.36, w * 0.38, h * 0.36)
            line(ctx, w * 0.38, h * 0.36, w * 0.46, h * 0.28)
            line(ctx, w * 0.46, h * 0.28, w * 0.84, h * 0.28)
            rect(ctx, w * 0.16, h * 0.36, w * 0.68, h * 0.42, false)
            break
        case "drive":
            rect(ctx, w * 0.16, h * 0.28, w * 0.68, h * 0.44, false)
            line(ctx, w * 0.24, h * 0.56, w * 0.64, h * 0.56)
            circle(ctx, w * 0.72, h * 0.56, w * 0.04, true)
            break
        case "cloud":
            ctx.beginPath()
            ctx.moveTo(w * 0.22, h * 0.64)
            ctx.bezierCurveTo(w * 0.14, h * 0.58, w * 0.18, h * 0.42, w * 0.34, h * 0.42)
            ctx.bezierCurveTo(w * 0.40, h * 0.26, w * 0.66, h * 0.28, w * 0.70, h * 0.48)
            ctx.bezierCurveTo(w * 0.86, h * 0.48, w * 0.90, h * 0.68, w * 0.76, h * 0.72)
            ctx.lineTo(w * 0.24, h * 0.72)
            ctx.stroke()
            break
        case "filter":
            poly(ctx, [[w * 0.16, h * 0.24], [w * 0.84, h * 0.24], [w * 0.58, h * 0.54], [w * 0.58, h * 0.78], [w * 0.42, h * 0.86], [w * 0.42, h * 0.54]], false)
            break
        case "search":
            circle(ctx, w * 0.44, h * 0.44, w * 0.22, false)
            line(ctx, w * 0.60, h * 0.60, w * 0.80, h * 0.80)
            break
        case "inspect":
            rect(ctx, w * 0.16, h * 0.18, w * 0.68, h * 0.64, false)
            line(ctx, w * 0.26, h * 0.34, w * 0.48, h * 0.34)
            line(ctx, w * 0.26, h * 0.47, w * 0.40, h * 0.47)
            circle(ctx, w * 0.59, h * 0.60, w * 0.10, false)
            line(ctx, w * 0.66, h * 0.67, w * 0.76, h * 0.77)
            break
        case "history":
        case "clock":
            circle(ctx, midX, midY, w * 0.30, false)
            line(ctx, midX, midY, midX, h * 0.32)
            line(ctx, midX, midY, w * 0.66, h * 0.58)
            break
        case "media":
            rect(ctx, w * 0.16, h * 0.2, w * 0.24, h * 0.24, false)
            rect(ctx, w * 0.16, h * 0.56, w * 0.24, h * 0.24, false)
            rect(ctx, w * 0.5, h * 0.2, w * 0.34, h * 0.24, false)
            rect(ctx, w * 0.5, h * 0.56, w * 0.34, h * 0.24, false)
            break
        case "image":
            rect(ctx, w * 0.16, h * 0.20, w * 0.68, h * 0.60, false)
            circle(ctx, w * 0.37, h * 0.38, w * 0.055, true)
            line(ctx, w * 0.22, h * 0.68, w * 0.44, h * 0.48)
            line(ctx, w * 0.44, h * 0.48, w * 0.58, h * 0.61)
            line(ctx, w * 0.58, h * 0.61, w * 0.72, h * 0.43)
            break
        case "play":
            poly(ctx, [[w * 0.3, h * 0.22], [w * 0.3, h * 0.78], [w * 0.76, midY]], true)
            break
        case "pause":
            rect(ctx, w * 0.26, h * 0.22, w * 0.16, h * 0.56, true)
            rect(ctx, w * 0.58, h * 0.22, w * 0.16, h * 0.56, true)
            break
        case "jump-start":
            line(ctx, w * 0.15, h * 0.24, w * 0.15, h * 0.76)
            poly(ctx, [[w * 0.49, h * 0.24], [w * 0.24, midY], [w * 0.49, h * 0.76]], true)
            poly(ctx, [[w * 0.81, h * 0.24], [w * 0.56, midY], [w * 0.81, h * 0.76]], true)
            break
        case "step-back":
            line(ctx, w * 0.20, h * 0.24, w * 0.20, h * 0.76)
            poly(ctx, [[w * 0.74, h * 0.24], [w * 0.34, midY], [w * 0.74, h * 0.76]], true)
            break
        case "step-forward":
            line(ctx, w * 0.80, h * 0.24, w * 0.80, h * 0.76)
            poly(ctx, [[w * 0.26, h * 0.24], [w * 0.66, midY], [w * 0.26, h * 0.76]], true)
            break
        case "cut":
            circle(ctx, w * 0.3, h * 0.68, w * 0.1, false)
            circle(ctx, w * 0.58, h * 0.68, w * 0.1, false)
            line(ctx, w * 0.24, h * 0.26, w * 0.68, h * 0.56)
            line(ctx, w * 0.68, h * 0.26, w * 0.36, h * 0.56)
            break
        case "trim":
            line(ctx, w * 0.20, h * 0.22, w * 0.20, h * 0.78)
            line(ctx, w * 0.80, h * 0.22, w * 0.80, h * 0.78)
            line(ctx, w * 0.30, h * 0.36, w * 0.70, h * 0.36)
            line(ctx, w * 0.30, h * 0.64, w * 0.70, h * 0.64)
            poly(ctx, [[w * 0.40, h * 0.40], [w * 0.40, h * 0.60],
                       [w * 0.58, midY]], true)
            break
        case "insert":
            line(ctx, midX, h * 0.18, midX, h * 0.62)
            poly(ctx, [[w * 0.34, h * 0.5], [midX, h * 0.74], [w * 0.66, h * 0.5]], true)
            line(ctx, w * 0.22, h * 0.82, w * 0.78, h * 0.82)
            break
        case "import":
            line(ctx, midX, h * 0.16, midX, h * 0.54)
            line(ctx, w * 0.34, h * 0.40, midX, h * 0.56)
            line(ctx, w * 0.66, h * 0.40, midX, h * 0.56)
            line(ctx, w * 0.22, h * 0.62, w * 0.22, h * 0.80)
            line(ctx, w * 0.22, h * 0.80, w * 0.78, h * 0.80)
            line(ctx, w * 0.78, h * 0.80, w * 0.78, h * 0.62)
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
        case "move-up":
            line(ctx, midX, h * 0.72, midX, h * 0.3)
            line(ctx, w * 0.3, h * 0.46, midX, h * 0.3)
            line(ctx, w * 0.7, h * 0.46, midX, h * 0.3)
            line(ctx, w * 0.24, h * 0.76, w * 0.76, h * 0.76)
            break
        case "move-down":
            line(ctx, midX, h * 0.28, midX, h * 0.7)
            line(ctx, w * 0.3, h * 0.54, midX, h * 0.7)
            line(ctx, w * 0.7, h * 0.54, midX, h * 0.7)
            line(ctx, w * 0.24, h * 0.24, w * 0.76, h * 0.24)
            break
        case "lock":
            rect(ctx, w * 0.24, h * 0.44, w * 0.52, h * 0.36, false)
            ctx.beginPath()
            ctx.arc(midX, h * 0.44, w * 0.18, Math.PI, 0, false)
            ctx.stroke()
            line(ctx, midX, h * 0.58, midX, h * 0.68)
            break
        case "trash":
        case "delete":
            line(ctx, w * 0.22, h * 0.28, w * 0.78, h * 0.28)
            line(ctx, w * 0.42, h * 0.18, w * 0.58, h * 0.18)
            rect(ctx, w * 0.28, h * 0.34, w * 0.44, h * 0.48, false)
            line(ctx, w * 0.42, h * 0.43, w * 0.42, h * 0.72)
            line(ctx, w * 0.58, h * 0.43, w * 0.58, h * 0.72)
            break
        case "distribute-horizontal":
            line(ctx, w * 0.16, h * 0.18, w * 0.16, h * 0.82)
            line(ctx, w * 0.84, h * 0.18, w * 0.84, h * 0.82)
            rect(ctx, w * 0.26, h * 0.34, w * 0.18, h * 0.32, false)
            rect(ctx, w * 0.56, h * 0.34, w * 0.18, h * 0.32, false)
            break
        case "distribute-vertical":
            line(ctx, w * 0.18, h * 0.16, w * 0.82, h * 0.16)
            line(ctx, w * 0.18, h * 0.84, w * 0.82, h * 0.84)
            rect(ctx, w * 0.34, h * 0.26, w * 0.32, h * 0.18, false)
            rect(ctx, w * 0.34, h * 0.56, w * 0.32, h * 0.18, false)
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
        case "move":
            line(ctx, midX, h * 0.16, midX, h * 0.84)
            line(ctx, w * 0.16, midY, w * 0.84, midY)
            line(ctx, w * 0.36, h * 0.30, midX, h * 0.16)
            line(ctx, w * 0.64, h * 0.30, midX, h * 0.16)
            line(ctx, w * 0.36, h * 0.70, midX, h * 0.84)
            line(ctx, w * 0.64, h * 0.70, midX, h * 0.84)
            line(ctx, w * 0.30, h * 0.36, w * 0.16, midY)
            line(ctx, w * 0.30, h * 0.64, w * 0.16, midY)
            line(ctx, w * 0.70, h * 0.36, w * 0.84, midY)
            line(ctx, w * 0.70, h * 0.64, w * 0.84, midY)
            break
        case "analyze":
            rect(ctx, w * 0.18, h * 0.57, w * 0.11, h * 0.23, false)
            rect(ctx, w * 0.40, h * 0.40, w * 0.11, h * 0.40, false)
            rect(ctx, w * 0.62, h * 0.22, w * 0.11, h * 0.58, false)
            line(ctx, w * 0.16, h * 0.84, w * 0.80, h * 0.84)
            break
        case "waveform":
            line(ctx, w * 0.16, midY, w * 0.25, midY)
            line(ctx, w * 0.29, h * 0.39, w * 0.29, h * 0.61)
            line(ctx, w * 0.39, h * 0.25, w * 0.39, h * 0.75)
            line(ctx, w * 0.50, h * 0.35, w * 0.50, h * 0.65)
            line(ctx, w * 0.61, h * 0.18, w * 0.61, h * 0.82)
            line(ctx, w * 0.72, h * 0.38, w * 0.72, h * 0.62)
            line(ctx, w * 0.77, midY, w * 0.84, midY)
            break
        case "audio":
            poly(ctx, [[w * 0.15, h * 0.39], [w * 0.31, h * 0.39], [w * 0.49, h * 0.23],
                       [w * 0.49, h * 0.77], [w * 0.31, h * 0.61], [w * 0.15, h * 0.61]], false)
            ctx.beginPath()
            ctx.arc(w * 0.48, midY, w * 0.32, -Math.PI / 3, Math.PI / 3)
            ctx.stroke()
            break
        case "audio-muted":
            poly(ctx, [[w * 0.15, h * 0.39], [w * 0.31, h * 0.39], [w * 0.49, h * 0.23],
                       [w * 0.49, h * 0.77], [w * 0.31, h * 0.61], [w * 0.15, h * 0.61]], false)
            line(ctx, w * 0.66, h * 0.38, w * 0.86, h * 0.62)
            line(ctx, w * 0.86, h * 0.38, w * 0.66, h * 0.62)
            break
        case "select":
            poly(ctx, [[w * 0.24, h * 0.18], [w * 0.24, h * 0.82], [w * 0.46, h * 0.62], [w * 0.62, h * 0.84], [w * 0.74, h * 0.76], [w * 0.58, h * 0.54], [w * 0.82, h * 0.54]], true)
            break
        case "hand":
            ctx.beginPath()
            ctx.moveTo(w * 0.30, h * 0.48)
            ctx.lineTo(w * 0.30, h * 0.28)
            ctx.quadraticCurveTo(w * 0.30, h * 0.20, w * 0.38, h * 0.20)
            ctx.quadraticCurveTo(w * 0.46, h * 0.20, w * 0.46, h * 0.28)
            ctx.lineTo(w * 0.46, h * 0.18)
            ctx.quadraticCurveTo(w * 0.46, h * 0.10, w * 0.54, h * 0.10)
            ctx.quadraticCurveTo(w * 0.62, h * 0.10, w * 0.62, h * 0.18)
            ctx.lineTo(w * 0.62, h * 0.30)
            ctx.quadraticCurveTo(w * 0.76, h * 0.24, w * 0.76, h * 0.38)
            ctx.lineTo(w * 0.76, h * 0.58)
            ctx.quadraticCurveTo(w * 0.76, h * 0.84, w * 0.52, h * 0.86)
            ctx.quadraticCurveTo(w * 0.36, h * 0.86, w * 0.26, h * 0.70)
            ctx.lineTo(w * 0.16, h * 0.54)
            ctx.quadraticCurveTo(w * 0.12, h * 0.46, w * 0.20, h * 0.42)
            ctx.quadraticCurveTo(w * 0.26, h * 0.40, w * 0.30, h * 0.48)
            ctx.stroke()
            break
        case "text-tool":
            line(ctx, w * 0.20, h * 0.20, w * 0.80, h * 0.20)
            line(ctx, midX, h * 0.20, midX, h * 0.80)
            line(ctx, w * 0.34, h * 0.80, w * 0.66, h * 0.80)
            break
        case "shape":
        case "shape-tool":
            rect(ctx, w * 0.16, h * 0.18, w * 0.48, h * 0.48, false)
            circle(ctx, w * 0.64, h * 0.64, w * 0.22, false)
            break
        case "brush-tool":
            line(ctx, w * 0.66, h * 0.16, w * 0.34, h * 0.58)
            line(ctx, w * 0.78, h * 0.28, w * 0.44, h * 0.66)
            ctx.beginPath()
            ctx.moveTo(w * 0.44, h * 0.66)
            ctx.quadraticCurveTo(w * 0.30, h * 0.88, w * 0.14, h * 0.82)
            ctx.quadraticCurveTo(w * 0.30, h * 0.76, w * 0.34, h * 0.58)
            ctx.stroke()
            break
        case "smart-select":
            line(ctx, w * 0.14, h * 0.34, w * 0.14, h * 0.18)
            line(ctx, w * 0.14, h * 0.18, w * 0.32, h * 0.18)
            line(ctx, w * 0.40, h * 0.18, w * 0.58, h * 0.18)
            line(ctx, w * 0.14, h * 0.44, w * 0.14, h * 0.60)
            line(ctx, w * 0.14, h * 0.70, w * 0.14, h * 0.82)
            line(ctx, w * 0.14, h * 0.82, w * 0.32, h * 0.82)
            line(ctx, w * 0.40, h * 0.82, w * 0.58, h * 0.82)
            line(ctx, w * 0.66, h * 0.82, w * 0.68, h * 0.82)
            line(ctx, w * 0.76, h * 0.24, w * 0.76, h * 0.46)
            line(ctx, w * 0.65, h * 0.35, w * 0.87, h * 0.35)
            line(ctx, w * 0.68, h * 0.64, w * 0.84, h * 0.80)
            break
        case "lasso":
            ctx.beginPath()
            ctx.moveTo(w * 0.28, h * 0.32)
            ctx.bezierCurveTo(w * 0.16, h * 0.16, w * 0.56, h * 0.10, w * 0.72, h * 0.28)
            ctx.bezierCurveTo(w * 0.88, h * 0.46, w * 0.76, h * 0.74, w * 0.52, h * 0.78)
            ctx.bezierCurveTo(w * 0.26, h * 0.84, w * 0.14, h * 0.52, w * 0.28, h * 0.32)
            ctx.stroke()
            line(ctx, w * 0.46, h * 0.78, w * 0.40, h * 0.88)
            circle(ctx, w * 0.38, h * 0.93, w * 0.035, true)
            break
        case "polygon-tool":
            poly(ctx, [[w * 0.24, h * 0.28], [w * 0.72, h * 0.22], [w * 0.80, h * 0.66], [w * 0.40, h * 0.80]], false)
            circle(ctx, w * 0.24, h * 0.28, w * 0.05, true)
            circle(ctx, w * 0.72, h * 0.22, w * 0.05, true)
            circle(ctx, w * 0.80, h * 0.66, w * 0.05, true)
            circle(ctx, w * 0.40, h * 0.80, w * 0.05, true)
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
        case "slide":
            rect(ctx, w * 0.32, h * 0.3, w * 0.36, h * 0.4, false)
            line(ctx, w * 0.12, midY, w * 0.3, midY)
            line(ctx, w * 0.12, midY, w * 0.23, h * 0.39)
            line(ctx, w * 0.12, midY, w * 0.23, h * 0.61)
            line(ctx, w * 0.7, midY, w * 0.88, midY)
            line(ctx, w * 0.88, midY, w * 0.77, h * 0.39)
            line(ctx, w * 0.88, midY, w * 0.77, h * 0.61)
            break
        case "link":
            ctx.beginPath()
            ctx.moveTo(w * 0.46, h * 0.65)
            ctx.lineTo(w * 0.35, h * 0.76)
            ctx.bezierCurveTo(w * 0.17, h * 0.94, w * 0.06, h * 0.62, w * 0.24, h * 0.45)
            ctx.lineTo(w * 0.35, h * 0.34)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(w * 0.54, h * 0.35)
            ctx.lineTo(w * 0.65, h * 0.24)
            ctx.bezierCurveTo(w * 0.83, h * 0.06, w * 0.94, h * 0.38, w * 0.76, h * 0.55)
            ctx.lineTo(w * 0.65, h * 0.66)
            ctx.stroke()
            line(ctx, w * 0.38, h * 0.62, w * 0.62, h * 0.38)
            break
        case "duplicate":
        case "copy":
            rect(ctx, w * 0.30, h * 0.22, w * 0.50, h * 0.54, false)
            line(ctx, w * 0.20, h * 0.34, w * 0.20, h * 0.84)
            line(ctx, w * 0.20, h * 0.84, w * 0.68, h * 0.84)
            break
        case "snap":
            ctx.beginPath()
            ctx.moveTo(w * 0.25, h * 0.20)
            ctx.lineTo(w * 0.25, h * 0.57)
            ctx.arc(midX, h * 0.57, w * 0.25, Math.PI, 0, true)
            ctx.lineTo(w * 0.75, h * 0.20)
            ctx.stroke()
            line(ctx, w * 0.25, h * 0.37, w * 0.35, h * 0.37)
            line(ctx, w * 0.65, h * 0.37, w * 0.75, h * 0.37)
            break
        case "ripple":
            line(ctx, midX, h * 0.22, midX, h * 0.78)
            line(ctx, w * 0.14, midY, w * 0.38, midY)
            line(ctx, w * 0.26, h * 0.38, w * 0.38, midY)
            line(ctx, w * 0.26, h * 0.62, w * 0.38, midY)
            line(ctx, w * 0.86, midY, w * 0.62, midY)
            line(ctx, w * 0.74, h * 0.38, w * 0.62, midY)
            line(ctx, w * 0.74, h * 0.62, w * 0.62, midY)
            break
        case "library":
            line(ctx, w * 0.18, h * 0.62, w * 0.18, h * 0.18)
            line(ctx, w * 0.18, h * 0.18, w * 0.66, h * 0.18)
            rect(ctx, w * 0.32, h * 0.32, w * 0.50, h * 0.50, false)
            ctx.beginPath()
            ctx.moveTo(w * 0.38, h * 0.70)
            ctx.lineTo(w * 0.51, h * 0.55)
            ctx.lineTo(w * 0.62, h * 0.66)
            ctx.lineTo(w * 0.75, h * 0.49)
            ctx.stroke()
            break
        case "adjust":
            line(ctx, w * 0.18, h * 0.28, w * 0.82, h * 0.28)
            line(ctx, w * 0.18, midY, w * 0.82, midY)
            line(ctx, w * 0.18, h * 0.72, w * 0.82, h * 0.72)
            circle(ctx, w * 0.34, h * 0.28, w * 0.06, true)
            circle(ctx, w * 0.62, midY, w * 0.06, true)
            circle(ctx, w * 0.46, h * 0.72, w * 0.06, true)
            break
        case "edit":
            poly(ctx, [[w * 0.20, h * 0.80], [w * 0.26, h * 0.58], [w * 0.66, h * 0.18],
                       [w * 0.82, h * 0.34], [w * 0.42, h * 0.74]], false)
            line(ctx, w * 0.57, h * 0.27, w * 0.73, h * 0.43)
            break
        case "masks":
            circle(ctx, w * 0.42, h * 0.44, w * 0.2, false)
            circle(ctx, w * 0.6, h * 0.56, w * 0.2, false)
            break
        case "layers":
            poly(ctx, [[midX, h * 0.15], [w * 0.84, h * 0.34], [midX, h * 0.53], [w * 0.16, h * 0.34]], false)
            ctx.beginPath()
            ctx.moveTo(w * 0.16, h * 0.51)
            ctx.lineTo(midX, h * 0.70)
            ctx.lineTo(w * 0.84, h * 0.51)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(w * 0.16, h * 0.68)
            ctx.lineTo(midX, h * 0.87)
            ctx.lineTo(w * 0.84, h * 0.68)
            ctx.stroke()
            break
        case "group":
            rect(ctx, w * 0.18, h * 0.22, w * 0.38, h * 0.38, false)
            rect(ctx, w * 0.44, h * 0.40, w * 0.38, h * 0.38, false)
            break
        case "carousel":
            rect(ctx, w * 0.31, h * 0.18, w * 0.38, h * 0.64, false)
            line(ctx, w * 0.16, h * 0.30, w * 0.16, h * 0.70)
            line(ctx, w * 0.84, h * 0.30, w * 0.84, h * 0.70)
            break
        case "timeline":
            rect(ctx, w * 0.15, h * 0.30, w * 0.32, h * 0.18, false)
            rect(ctx, w * 0.33, h * 0.64, w * 0.52, h * 0.18, false)
            line(ctx, w * 0.64, h * 0.21, w * 0.64, h * 0.88)
            poly(ctx, [[w * 0.55, h * 0.12], [w * 0.73, h * 0.12], [w * 0.64, h * 0.24]], true)
            break
        case "export":
            line(ctx, midX, h * 0.18, midX, h * 0.54)
            line(ctx, midX, h * 0.18, w * 0.66, h * 0.34)
            line(ctx, midX, h * 0.18, w * 0.34, h * 0.34)
            line(ctx, w * 0.24, h * 0.52, w * 0.24, h * 0.76)
            line(ctx, w * 0.76, h * 0.52, w * 0.76, h * 0.76)
            line(ctx, w * 0.24, h * 0.76, w * 0.76, h * 0.76)
            line(ctx, w * 0.24, h * 0.52, w * 0.38, h * 0.52)
            line(ctx, w * 0.62, h * 0.52, w * 0.76, h * 0.52)
            break
        case "render":
            rect(ctx, w * 0.16, h * 0.19, w * 0.68, h * 0.48, false)
            line(ctx, w * 0.28, h * 0.75, w * 0.72, h * 0.75)
            circle(ctx, w * 0.39, h * 0.43, w * 0.055, true)
            circle(ctx, w * 0.56, h * 0.43, w * 0.055, true)
            circle(ctx, w * 0.73, h * 0.43, w * 0.055, true)
            break
        case "grid":
            rect(ctx, w * 0.18, h * 0.2, w * 0.22, h * 0.22, false)
            rect(ctx, w * 0.6, h * 0.2, w * 0.22, h * 0.22, false)
            rect(ctx, w * 0.18, h * 0.58, w * 0.22, h * 0.22, false)
            rect(ctx, w * 0.6, h * 0.58, w * 0.22, h * 0.22, false)
            break
        case "list":
            line(ctx, w * 0.34, h * 0.28, w * 0.82, h * 0.28)
            line(ctx, w * 0.34, midY, w * 0.82, midY)
            line(ctx, w * 0.34, h * 0.72, w * 0.82, h * 0.72)
            circle(ctx, w * 0.2, h * 0.28, w * 0.035, true)
            circle(ctx, w * 0.2, midY, w * 0.035, true)
            circle(ctx, w * 0.2, h * 0.72, w * 0.035, true)
            break
        case "survey":
            rect(ctx, w * 0.18, h * 0.24, w * 0.28, h * 0.22, false)
            rect(ctx, w * 0.54, h * 0.24, w * 0.28, h * 0.22, false)
            rect(ctx, w * 0.18, h * 0.54, w * 0.64, h * 0.22, false)
            break
        case "retry":
        case "refresh":
            ctx.beginPath()
            ctx.arc(midX, midY, w * 0.30, Math.PI * 0.25, Math.PI * 1.85)
            ctx.stroke()
            line(ctx, w * 0.77, h * 0.18, w * 0.77, h * 0.36)
            line(ctx, w * 0.77, h * 0.36, w * 0.59, h * 0.36)
            break
        case "undo":
            ctx.beginPath()
            ctx.moveTo(w * 0.18, h * 0.39)
            ctx.lineTo(w * 0.55, h * 0.39)
            ctx.bezierCurveTo(w * 0.88, h * 0.39, w * 0.88, h * 0.78, w * 0.55, h * 0.78)
            ctx.stroke()
            line(ctx, w * 0.18, h * 0.39, w * 0.35, h * 0.22)
            line(ctx, w * 0.18, h * 0.39, w * 0.35, h * 0.56)
            break
        case "redo":
            ctx.beginPath()
            ctx.moveTo(w * 0.82, h * 0.39)
            ctx.lineTo(w * 0.45, h * 0.39)
            ctx.bezierCurveTo(w * 0.12, h * 0.39, w * 0.12, h * 0.78, w * 0.45, h * 0.78)
            ctx.stroke()
            line(ctx, w * 0.82, h * 0.39, w * 0.65, h * 0.22)
            line(ctx, w * 0.82, h * 0.39, w * 0.65, h * 0.56)
            break
        case "fit":
            rect(ctx, w * 0.16, h * 0.20, w * 0.68, h * 0.60, false)
            rect(ctx, w * 0.30, h * 0.34, w * 0.40, h * 0.32, false)
            break
        case "fill":
            rect(ctx, w * 0.20, h * 0.20, w * 0.60, h * 0.60, false)
            rect(ctx, w * 0.32, h * 0.32, w * 0.36, h * 0.36, true)
            break
        case "zoom":
            circle(ctx, w * 0.42, h * 0.42, w * 0.22, false)
            line(ctx, w * 0.58, h * 0.58, w * 0.80, h * 0.80)
            line(ctx, w * 0.34, h * 0.42, w * 0.50, h * 0.42)
            line(ctx, w * 0.42, h * 0.34, w * 0.42, h * 0.50)
            break
        case "crop":
            line(ctx, w * 0.22, h * 0.18, w * 0.22, h * 0.64)
            line(ctx, w * 0.18, h * 0.22, w * 0.64, h * 0.22)
            line(ctx, w * 0.78, h * 0.36, w * 0.78, h * 0.82)
            line(ctx, w * 0.36, h * 0.78, w * 0.82, h * 0.78)
            break
        case "local-adjust":
            line(ctx, w * 0.18, h * 0.72, w * 0.72, h * 0.18)
            line(ctx, w * 0.48, h * 0.18, w * 0.72, h * 0.18)
            line(ctx, w * 0.72, h * 0.18, w * 0.72, h * 0.42)
            circle(ctx, w * 0.32, h * 0.68, w * 0.08, false)
            break
        case "retouch":
            line(ctx, w * 0.22, h * 0.72, w * 0.68, h * 0.26)
            line(ctx, w * 0.58, h * 0.16, w * 0.78, h * 0.36)
            line(ctx, w * 0.28, h * 0.78, w * 0.18, h * 0.86)
            circle(ctx, w * 0.74, h * 0.22, w * 0.07, false)
            break
        case "geometry":
            rect(ctx, w * 0.24, h * 0.24, w * 0.52, h * 0.52, false)
            circle(ctx, w * 0.24, h * 0.24, w * 0.055, true)
            circle(ctx, w * 0.76, h * 0.24, w * 0.055, true)
            circle(ctx, w * 0.24, h * 0.76, w * 0.055, true)
            circle(ctx, w * 0.76, h * 0.76, w * 0.055, true)
            break
        case "scope":
            line(ctx, w * 0.18, h * 0.78, w * 0.82, h * 0.78)
            ctx.beginPath()
            ctx.moveTo(w * 0.18, h * 0.62)
            ctx.bezierCurveTo(w * 0.32, h * 0.62, w * 0.36, h * 0.28, w * 0.5, h * 0.28)
            ctx.bezierCurveTo(w * 0.64, h * 0.28, w * 0.68, h * 0.62, w * 0.82, h * 0.62)
            ctx.stroke()
            break
        case "menu":
            line(ctx, w * 0.24, h * 0.32, w * 0.76, h * 0.32)
            line(ctx, w * 0.24, midY, w * 0.76, midY)
            line(ctx, w * 0.24, h * 0.68, w * 0.76, h * 0.68)
            break
        case "more":
            circle(ctx, w * 0.25, midY, w * 0.055, true)
            circle(ctx, midX, midY, w * 0.055, true)
            circle(ctx, w * 0.75, midY, w * 0.055, true)
            break
        case "target":
            circle(ctx, midX, midY, w * 0.27, false)
            circle(ctx, midX, midY, w * 0.075, false)
            line(ctx, midX, h * 0.10, midX, h * 0.27)
            line(ctx, midX, h * 0.73, midX, h * 0.90)
            line(ctx, w * 0.10, midY, w * 0.27, midY)
            line(ctx, w * 0.73, midY, w * 0.90, midY)
            break
        case "sparkles":
            poly(ctx, [[w * 0.43, h * 0.10], [w * 0.51, h * 0.34],
                       [w * 0.75, h * 0.42], [w * 0.51, h * 0.50],
                       [w * 0.43, h * 0.74], [w * 0.35, h * 0.50],
                       [w * 0.11, h * 0.42], [w * 0.35, h * 0.34]], false)
            line(ctx, w * 0.77, h * 0.66, w * 0.77, h * 0.86)
            line(ctx, w * 0.67, h * 0.76, w * 0.87, h * 0.76)
            break
        case "info":
            circle(ctx, midX, midY, w * 0.32, false)
            circle(ctx, midX, h * 0.34, w * 0.035, true)
            line(ctx, midX, h * 0.48, midX, h * 0.68)
            break
        case "warning":
            ctx.beginPath()
            ctx.moveTo(midX, h * 0.17)
            ctx.lineTo(w * 0.86, h * 0.79)
            ctx.lineTo(w * 0.14, h * 0.79)
            ctx.closePath()
            ctx.stroke()
            line(ctx, midX, h * 0.38, midX, h * 0.57)
            circle(ctx, midX, h * 0.67, w * 0.025, true)
            break
        case "reset":
            ctx.beginPath()
            ctx.arc(midX, midY, w * 0.30, Math.PI * 0.15, Math.PI * 1.18, true)
            ctx.stroke()
            line(ctx, w * 0.24, h * 0.18, w * 0.24, h * 0.34)
            line(ctx, w * 0.24, h * 0.34, w * 0.42, h * 0.34)
            break
        case "swap":
            line(ctx, w * 0.20, h * 0.36, w * 0.80, h * 0.36)
            line(ctx, w * 0.64, h * 0.20, w * 0.80, h * 0.36)
            line(ctx, w * 0.64, h * 0.52, w * 0.80, h * 0.36)
            line(ctx, w * 0.80, h * 0.64, w * 0.20, h * 0.64)
            line(ctx, w * 0.36, h * 0.48, w * 0.20, h * 0.64)
            line(ctx, w * 0.36, h * 0.80, w * 0.20, h * 0.64)
            break
        case "paste":
            rect(ctx, w * 0.23, h * 0.22, w * 0.56, h * 0.62, false)
            rect(ctx, w * 0.37, h * 0.14, w * 0.28, h * 0.16, false)
            line(ctx, w * 0.34, h * 0.47, w * 0.68, h * 0.47)
            line(ctx, w * 0.34, h * 0.60, w * 0.61, h * 0.60)
            break
        case "captions":
            rect(ctx, w * 0.14, h * 0.23, w * 0.72, h * 0.54, false)
            line(ctx, w * 0.25, h * 0.46, w * 0.75, h * 0.46)
            line(ctx, w * 0.32, h * 0.60, w * 0.68, h * 0.60)
            break
        case "compare":
            rect(ctx, w * 0.12, h * 0.2, w * 0.76, h * 0.6, false)
            line(ctx, midX, h * 0.21, midX, h * 0.79)
            line(ctx, w * 0.43, h * 0.50, w * 0.57, h * 0.50)
            break
        case "visible":
            ctx.beginPath()
            ctx.moveTo(w * 0.14, midY)
            ctx.quadraticCurveTo(midX, h * 0.22, w * 0.86, midY)
            ctx.quadraticCurveTo(midX, h * 0.78, w * 0.14, midY)
            ctx.stroke()
            circle(ctx, midX, midY, w * 0.13, false)
            circle(ctx, midX, midY, w * 0.035, true)
            break
        case "hidden":
            ctx.beginPath()
            ctx.moveTo(w * 0.14, midY)
            ctx.quadraticCurveTo(midX, h * 0.24, w * 0.86, midY)
            ctx.quadraticCurveTo(midX, h * 0.76, w * 0.14, midY)
            ctx.stroke()
            circle(ctx, midX, midY, w * 0.12, false)
            line(ctx, w * 0.22, h * 0.82, w * 0.78, h * 0.18)
            break
        case "panel-left":
            rect(ctx, w * 0.16, h * 0.20, w * 0.68, h * 0.60, false)
            line(ctx, w * 0.40, h * 0.22, w * 0.40, h * 0.78)
            line(ctx, w * 0.24, h * 0.34, w * 0.32, h * 0.34)
            line(ctx, w * 0.24, midY, w * 0.32, midY)
            line(ctx, w * 0.24, h * 0.66, w * 0.32, h * 0.66)
            break
        case "panel-right":
            rect(ctx, w * 0.16, h * 0.20, w * 0.68, h * 0.60, false)
            line(ctx, w * 0.60, h * 0.22, w * 0.60, h * 0.78)
            line(ctx, w * 0.68, h * 0.34, w * 0.76, h * 0.34)
            line(ctx, w * 0.68, midY, w * 0.76, midY)
            line(ctx, w * 0.68, h * 0.66, w * 0.76, h * 0.66)
            break
        case "density":
            line(ctx, w * 0.20, h * 0.30, w * 0.80, h * 0.30)
            line(ctx, w * 0.20, midY, w * 0.68, midY)
            line(ctx, w * 0.20, h * 0.70, w * 0.56, h * 0.70)
            circle(ctx, w * 0.80, h * 0.30, w * 0.035, true)
            circle(ctx, w * 0.68, midY, w * 0.035, true)
            circle(ctx, w * 0.56, h * 0.70, w * 0.035, true)
            break
        case "maximize":
        case "fullscreen":
            line(ctx, w * 0.20, h * 0.40, w * 0.20, h * 0.20)
            line(ctx, w * 0.20, h * 0.20, w * 0.40, h * 0.20)
            line(ctx, w * 0.60, h * 0.20, w * 0.80, h * 0.20)
            line(ctx, w * 0.80, h * 0.20, w * 0.80, h * 0.40)
            line(ctx, w * 0.80, h * 0.60, w * 0.80, h * 0.80)
            line(ctx, w * 0.80, h * 0.80, w * 0.60, h * 0.80)
            line(ctx, w * 0.40, h * 0.80, w * 0.20, h * 0.80)
            line(ctx, w * 0.20, h * 0.80, w * 0.20, h * 0.60)
            break
        case "focus-frame":
            line(ctx, w * 0.20, h * 0.36, w * 0.20, h * 0.20)
            line(ctx, w * 0.20, h * 0.20, w * 0.36, h * 0.20)
            line(ctx, w * 0.64, h * 0.20, w * 0.80, h * 0.20)
            line(ctx, w * 0.80, h * 0.20, w * 0.80, h * 0.36)
            line(ctx, w * 0.80, h * 0.64, w * 0.80, h * 0.80)
            line(ctx, w * 0.80, h * 0.80, w * 0.64, h * 0.80)
            line(ctx, w * 0.36, h * 0.80, w * 0.20, h * 0.80)
            line(ctx, w * 0.20, h * 0.80, w * 0.20, h * 0.64)
            circle(ctx, midX, midY, w * 0.10, false)
            break
        case "split-view":
            rect(ctx, w * 0.18, h * 0.22, w * 0.64, h * 0.56, false)
            line(ctx, midX, h * 0.24, midX, h * 0.76)
            break
        case "wipe":
            rect(ctx, w * 0.18, h * 0.22, w * 0.64, h * 0.56, false)
            line(ctx, w * 0.34, h * 0.24, w * 0.66, h * 0.76)
            break
        case "eyedropper":
            line(ctx, w * 0.3, h * 0.7, w * 0.68, h * 0.32)
            line(ctx, w * 0.42, h * 0.82, w * 0.8, h * 0.44)
            circle(ctx, w * 0.28, h * 0.74, w * 0.06, false)
            rect(ctx, w * 0.68, h * 0.2, w * 0.12, h * 0.12, false)
            break
        case "agent-terminal":
            rect(ctx, w * 0.16, h * 0.16, w * 0.68, h * 0.58, false)
            line(ctx, w * 0.25, h * 0.31, w * 0.75, h * 0.31)
            line(ctx, w * 0.27, h * 0.46, w * 0.39, h * 0.46)
            line(ctx, w * 0.46, h * 0.46, w * 0.70, h * 0.46)
            line(ctx, midX, h * 0.74, midX, h * 0.84)
            line(ctx, w * 0.34, h * 0.84, w * 0.66, h * 0.84)
            break
        case "error":
            circle(ctx, midX, midY, w * 0.33, false)
            line(ctx, midX, h * 0.30, midX, h * 0.53)
            circle(ctx, midX, h * 0.68, w * 0.035, true)
            break
        case "external-link":
            ctx.beginPath()
            ctx.moveTo(w * 0.43, h * 0.20)
            ctx.lineTo(w * 0.20, h * 0.20)
            ctx.lineTo(w * 0.20, h * 0.80)
            ctx.lineTo(w * 0.80, h * 0.80)
            ctx.lineTo(w * 0.80, h * 0.57)
            ctx.stroke()
            line(ctx, w * 0.47, h * 0.53, w * 0.80, h * 0.20)
            line(ctx, w * 0.59, h * 0.20, w * 0.80, h * 0.20)
            line(ctx, w * 0.80, h * 0.20, w * 0.80, h * 0.41)
            break
        case "solo":
            ctx.beginPath()
            ctx.moveTo(w * 0.72, h * 0.29)
            ctx.bezierCurveTo(w * 0.30, h * 0.04, w * 0.12, h * 0.45, midX, midY)
            ctx.bezierCurveTo(w * 0.88, h * 0.55, w * 0.70, h * 0.96, w * 0.28, h * 0.71)
            ctx.stroke()
            break
        case "unlock":
            rect(ctx, w * 0.24, h * 0.44, w * 0.52, h * 0.36, false)
            ctx.beginPath()
            ctx.arc(midX, h * 0.44, w * 0.18, Math.PI, -Math.PI / 5)
            ctx.stroke()
            line(ctx, midX, h * 0.58, midX, h * 0.68)
            break
        case "stop":
            rect(ctx, w * 0.26, h * 0.26, w * 0.48, h * 0.48, true)
            break
        case "record":
            circle(ctx, midX, midY, w * 0.29, true)
            break
        default:
            // Unknown names must be visibly diagnosable, never a misleading minus action.
            circle(ctx, midX, midY, w * 0.32, false)
            ctx.beginPath()
            ctx.moveTo(w * 0.40, h * 0.38)
            ctx.bezierCurveTo(w * 0.40, h * 0.25, w * 0.67, h * 0.27, w * 0.59, h * 0.43)
            ctx.lineTo(midX, h * 0.53)
            ctx.stroke()
            circle(ctx, midX, h * 0.66, w * 0.025, true)
            break
        }
    }
}
