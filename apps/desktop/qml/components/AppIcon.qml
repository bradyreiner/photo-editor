import QtQuick

Canvas {
    id: root

    required property string name
    property color color: "#E2E2E2"
    property real strokeScale: Math.max(1.15, Math.min(width, height) * 0.0725)
    readonly property string iconSystemContract: "app-icon:v2|canvas:16-grid|aspect:square-centered|stroke:optically-scaled|caps:round|joins:round|baseline:center|state-color:caller-semantic"

    readonly property string iconDesignRevision: "looksy-icons:3|rounded-outline|single-metaphor|24-unit-artwork|semantic-color"

    implicitWidth: 16
    implicitHeight: 16
    // Acquire the context during paint. Eager contextType creation is queued
    // by Qt and can run after a virtualized icon has left its window.
    antialiasing: true
    smooth: true

    onVisibleChanged: if (visible) requestPaint()
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
        const radius = Math.min(Math.min(w, h) * 0.18, Math.min(root.width, root.height) * 0.055)
        ctx.moveTo(x + radius, y)
        ctx.lineTo(x + w - radius, y)
        ctx.quadraticCurveTo(x + w, y, x + w, y + radius)
        ctx.lineTo(x + w, y + h - radius)
        ctx.quadraticCurveTo(x + w, y + h, x + w - radius, y + h)
        ctx.lineTo(x + radius, y + h)
        ctx.quadraticCurveTo(x, y + h, x, y + h - radius)
        ctx.lineTo(x, y + radius)
        ctx.quadraticCurveTo(x, y, x + radius, y)
        ctx.closePath()
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

    // Artwork uses a common 24-unit square; scale coordinates rather than
    // the context so all glyphs keep the caller's optical stroke weight.
    function path(ctx, commands, fill) {
        const unit = Math.min(width, height) / 24
        ctx.beginPath()
        for (let i = 0; i < commands.length; ++i) {
            const c = commands[i]
            if (c[0] === "M") ctx.moveTo(c[1] * unit, c[2] * unit)
            else if (c[0] === "L") ctx.lineTo(c[1] * unit, c[2] * unit)
            else if (c[0] === "Q") ctx.quadraticCurveTo(c[1] * unit, c[2] * unit, c[3] * unit, c[4] * unit)
            else if (c[0] === "C") ctx.bezierCurveTo(c[1] * unit, c[2] * unit, c[3] * unit, c[4] * unit, c[5] * unit, c[6] * unit)
            else if (c[0] === "Z") ctx.closePath()
        }
        if (fill) ctx.fill()
        else ctx.stroke()
    }

    function sparkle(ctx, x, y, radius) {
        path(ctx, [["M", x, y - radius], ["Q", x, y, x + radius, y],
                   ["Q", x, y, x, y + radius], ["Q", x, y, x - radius, y],
                   ["Q", x, y, x, y - radius], ["Z"]], false)
    }

    function paintBrush(ctx) {
        path(ctx, [["M", 10, 15], ["L", 17.5, 4.5], ["Q", 19, 2.5, 20.5, 4],
                   ["Q", 22, 5.5, 20, 7], ["L", 12, 17], ["Z"]], false)
        path(ctx, [["M", 10, 15], ["C", 4, 14, 8, 20, 3.5, 20],
                   ["C", 8, 23, 13, 20, 12, 17]], false)
    }

    onPaint: {
        if (!visible || !available || width <= 0 || height <= 0)
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
        case "color-range":
            for (const center of [[0.36, 0.38], [0.64, 0.38], [0.5, 0.64]]) {
                ctx.beginPath()
                ctx.arc(w * center[0], h * center[1], w * 0.23, 0, Math.PI * 2)
                ctx.stroke()
            }
            break
        case "selection-new":
        case "selection-add":
        case "selection-subtract":
        case "selection-intersect":
            ctx.setLineDash([w * 0.09, w * 0.07])
            rect(ctx, w * 0.10, h * 0.12, w * 0.53, h * 0.57, false)
            ctx.setLineDash([])
            if (root.name === "selection-new") {
                rect(ctx, w * 0.40, h * 0.40, w * 0.48, h * 0.48, false)
            } else if (root.name === "selection-intersect") {
                rect(ctx, w * 0.39, h * 0.39, w * 0.48, h * 0.48, false)
                rect(ctx, w * 0.39, h * 0.39, w * 0.24, h * 0.30, true)
            } else {
                line(ctx, w * 0.53, h * 0.74, w * 0.91, h * 0.74)
                if (root.name === "selection-add") line(ctx, w * 0.72, h * 0.55, w * 0.72, h * 0.93)
            }
            break
        case "text":
        case "text-tool":
            line(ctx, w * 0.20, h * 0.20, w * 0.80, h * 0.20)
            line(ctx, midX, h * 0.20, midX, h * 0.82)
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
        case "flag":
            line(ctx, w * 0.24, h * 0.14, w * 0.24, h * 0.90)
            poly(ctx, [[w * 0.24, h * 0.15], [w * 0.80, h * 0.15], [w * 0.67, h * 0.36], [w * 0.80, h * 0.57], [w * 0.24, h * 0.57]], true)
            break
        case "star-gold": {
            const points = []
            for (let i = 0; i < 10; ++i) {
                const angle = -Math.PI / 2 + i * Math.PI / 5
                const radius = i % 2 === 0 ? w * 0.44 : w * 0.205
                points.push([midX + Math.cos(angle) * radius, midY + Math.sin(angle) * radius])
            }
            const gold = ctx.createLinearGradient(0, h * 0.10, 0, h * 0.90)
            gold.addColorStop(0, "#FFE49A")
            gold.addColorStop(1, "#D5A43B")
            ctx.fillStyle = gold
            poly(ctx, points, true)
            break
        }
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
            rect(ctx, w * 0.20, h * 0.16, w * 0.60, h * 0.68, false)
            rect(ctx, w * 0.34, h * 0.16, w * 0.32, h * 0.24, false)
            rect(ctx, w * 0.33, h * 0.59, w * 0.34, h * 0.25, false)
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
            path(ctx, [["M", 9, 4], ["L", 10, 2.8], ["L", 14, 2.8], ["L", 15, 5],
                       ["L", 17.5, 5.3], ["L", 19.4, 8.5], ["L", 18, 10.5],
                       ["L", 18, 13.5], ["L", 19.4, 15.5], ["L", 17.5, 18.7],
                       ["L", 15, 19], ["L", 14, 21.2], ["L", 10, 21.2],
                       ["L", 9, 19], ["L", 6.5, 18.7], ["L", 4.6, 15.5],
                       ["L", 6, 13.5], ["L", 6, 10.5], ["L", 4.6, 8.5],
                       ["L", 6.5, 5.3], ["L", 9, 5], ["Z"]], false)
            circle(ctx, midX, midY, w * 0.13, false)
            break
        case "video":
        case "source":
            rect(ctx, w * 0.16, h * 0.24, w * 0.68, h * 0.52, false)
            poly(ctx, [[w * 0.44, h * 0.38], [w * 0.44, h * 0.62], [w * 0.62, midY]], true)
            break
        case "folder":
            path(ctx, [["M", 3.5, 8], ["L", 3.5, 6], ["Q", 3.5, 4, 5.5, 4],
                       ["L", 9, 4], ["L", 11, 7], ["L", 18.5, 7], ["Q", 20.5, 7, 20.5, 9],
                       ["L", 20.5, 18], ["Q", 20.5, 20, 18.5, 20], ["L", 5.5, 20],
                       ["Q", 3.5, 20, 3.5, 18], ["L", 3.5, 8], ["L", 20.5, 8]], false)
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
            circle(ctx, w * 0.44, h * 0.43, w * 0.27, false)
            line(ctx, w * 0.64, h * 0.64, w * 0.84, h * 0.84)
            line(ctx, w * 0.32, h * 0.38, w * 0.55, h * 0.38)
            line(ctx, w * 0.32, h * 0.49, w * 0.47, h * 0.49)
            break
        case "history":
        case "clock":
            circle(ctx, midX, midY, w * 0.30, false)
            line(ctx, midX, midY, midX, h * 0.32)
            line(ctx, midX, midY, w * 0.66, h * 0.58)
            break
        case "media":
            rect(ctx, w * 0.15, h * 0.20, w * 0.70, h * 0.60, false)
            line(ctx, w * 0.26, h * 0.65, w * 0.44, h * 0.45)
            line(ctx, w * 0.44, h * 0.45, w * 0.61, h * 0.63)
            line(ctx, w * 0.61, h * 0.63, w * 0.75, h * 0.48)
            circle(ctx, w * 0.67, h * 0.35, w * 0.045, true)
            break
        case "image":
            rect(ctx, w * 0.15, h * 0.20, w * 0.70, h * 0.60, false)
            path(ctx, [["M", 4, 17], ["L", 9, 11], ["L", 13, 15], ["L", 16, 12], ["L", 20, 17]], false)
            circle(ctx, w * 0.66, h * 0.36, w * 0.05, true)
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
        case "jump-end":
            line(ctx, w * 0.85, h * 0.24, w * 0.85, h * 0.76)
            poly(ctx, [[w * 0.51, h * 0.24], [w * 0.76, midY], [w * 0.51, h * 0.76]], true)
            poly(ctx, [[w * 0.19, h * 0.24], [w * 0.44, midY], [w * 0.19, h * 0.76]], true)
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
            circle(ctx, w * 0.28, h * 0.29, w * 0.12, false)
            circle(ctx, w * 0.28, h * 0.71, w * 0.12, false)
            line(ctx, w * 0.38, h * 0.38, w * 0.82, h * 0.81)
            line(ctx, w * 0.38, h * 0.62, w * 0.82, h * 0.19)
            break
        case "trim":
            path(ctx, [["M", 8, 4], ["L", 5, 4], ["L", 5, 20], ["L", 8, 20]], false)
            path(ctx, [["M", 16, 4], ["L", 19, 4], ["L", 19, 20], ["L", 16, 20]], false)
            line(ctx, w * 0.37, midY, w * 0.63, midY)
            break
        case "insert":
            line(ctx, midX, h * 0.17, midX, h * 0.65)
            path(ctx, [["M", 8, 12], ["L", 12, 16], ["L", 16, 12]], false)
            path(ctx, [["M", 4, 17], ["L", 4, 20], ["L", 20, 20], ["L", 20, 17]], false)
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
            rect(ctx, w * 0.18, h * 0.23, w * 0.64, h * 0.54, false)
            line(ctx, w * 0.35, midY, w * 0.65, midY)
            path(ctx, [["M", 12, 8], ["L", 16, 12], ["L", 12, 16]], false)
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
            sparkle(ctx, 10, 11, 7)
            sparkle(ctx, 19, 5, 2.5)
            line(ctx, w * 0.76, h * 0.70, w * 0.76, h * 0.87)
            line(ctx, w * 0.68, h * 0.78, w * 0.84, h * 0.78)
            break
        case "transition":
            path(ctx, [["M", 4, 5], ["L", 10, 12], ["L", 4, 19]], false)
            path(ctx, [["M", 20, 5], ["L", 14, 12], ["L", 20, 19]], false)
            line(ctx, midX, h * 0.30, midX, h * 0.70)
            break
        case "motion":
            path(ctx, [["M", 4, 9], ["L", 4, 4], ["L", 9, 4]], false)
            path(ctx, [["M", 15, 20], ["L", 20, 20], ["L", 20, 15]], false)
            line(ctx, w * 0.34, h * 0.66, w * 0.68, h * 0.32)
            path(ctx, [["M", 11, 7.5], ["L", 16.5, 7.5], ["L", 16.5, 13]], false)
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
            line(ctx, w * 0.24, h * 0.80, w * 0.24, h * 0.55)
            line(ctx, midX, h * 0.80, midX, h * 0.37)
            line(ctx, w * 0.76, h * 0.80, w * 0.76, h * 0.18)
            break
        case "waveform":
            line(ctx, w * 0.20, h * 0.40, w * 0.20, h * 0.60)
            line(ctx, w * 0.35, h * 0.23, w * 0.35, h * 0.77)
            line(ctx, midX, h * 0.34, midX, h * 0.66)
            line(ctx, w * 0.65, h * 0.17, w * 0.65, h * 0.83)
            line(ctx, w * 0.80, h * 0.40, w * 0.80, h * 0.60)
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
            path(ctx, [["M", 6, 3.5], ["L", 6, 20], ["L", 10.5, 15.5],
                       ["L", 13.5, 21], ["L", 17, 19], ["L", 14, 14], ["L", 20, 13], ["Z"]], false)
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
        case "shape":
        case "shape-tool":
            rect(ctx, w * 0.16, h * 0.18, w * 0.48, h * 0.48, false)
            circle(ctx, w * 0.64, h * 0.64, w * 0.22, false)
            break
        case "brush-tool":
            paintBrush(ctx)
            break
        case "smart-select":
            path(ctx, [["M", 8, 4], ["L", 4, 4], ["L", 4, 8]], false)
            path(ctx, [["M", 16, 4], ["L", 20, 4], ["L", 20, 8]], false)
            path(ctx, [["M", 20, 16], ["L", 20, 20], ["L", 16, 20]], false)
            path(ctx, [["M", 8, 20], ["L", 4, 20], ["L", 4, 16]], false)
            line(ctx, w * 0.47, h * 0.17, w * 0.53, h * 0.17)
            line(ctx, w * 0.83, h * 0.47, w * 0.83, h * 0.53)
            line(ctx, w * 0.47, h * 0.83, w * 0.53, h * 0.83)
            line(ctx, w * 0.17, h * 0.47, w * 0.17, h * 0.53)
            break
        case "lasso":
            path(ctx, [["M", 8, 17], ["C", 1, 14, 4, 4, 12, 4],
                       ["C", 23, 4, 22, 16, 12, 18], ["C", 6, 19, 7, 13, 10, 15],
                       ["C", 13, 17, 10, 21, 7, 21]], false)
            break
        case "polygon-tool":
            poly(ctx, [[w * 0.22, h * 0.28], [w * 0.73, h * 0.20],
                       [w * 0.83, h * 0.67], [w * 0.37, h * 0.83]], false)
            circle(ctx, w * 0.22, h * 0.28, w * 0.055, true)
            circle(ctx, w * 0.73, h * 0.20, w * 0.055, true)
            circle(ctx, w * 0.83, h * 0.67, w * 0.055, true)
            circle(ctx, w * 0.37, h * 0.83, w * 0.055, true)
            break
        case "brush-subtract":
            paintBrush(ctx)
            line(ctx, w * 0.13, h * 0.22, w * 0.36, h * 0.22)
            break
        case "erase-tool":
            path(ctx, [["M", 4, 14], ["L", 12.5, 4], ["Q", 13.5, 3, 14.5, 4],
                       ["L", 20, 9.5], ["Q", 21, 10.5, 20, 11.5], ["L", 12.5, 20],
                       ["L", 9, 20], ["L", 4, 15.5], ["Q", 3.5, 15, 4, 14]], false)
            line(ctx, w * 0.36, h * 0.39, w * 0.70, h * 0.72)
            break
        case "invert":
            circle(ctx, midX, midY, w * 0.33, false)
            ctx.beginPath()
            ctx.arc(midX, midY, w * 0.33, -Math.PI / 2, Math.PI / 2)
            ctx.closePath()
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
            path(ctx, [["M", 9, 15], ["L", 6.5, 17.5], ["C", 2, 22, 0.5, 15, 4, 11.5],
                       ["L", 7, 8.5], ["C", 9.5, 6, 12, 7, 13, 9]], false)
            path(ctx, [["M", 15, 9], ["L", 17.5, 6.5], ["C", 22, 2, 23.5, 9, 20, 12.5],
                       ["L", 17, 15.5], ["C", 14.5, 18, 12, 17, 11, 15]], false)
            line(ctx, w * 0.38, h * 0.62, w * 0.62, h * 0.38)
            break
        case "unlink":
            path(ctx, [["M", 9, 15], ["L", 6.5, 17.5], ["C", 2, 22, 0.5, 15, 4, 11.5], ["L", 6, 9.5]], false)
            path(ctx, [["M", 15, 9], ["L", 17.5, 6.5], ["C", 22, 2, 23.5, 9, 20, 12.5], ["L", 18, 14.5]], false)
            line(ctx, w * 0.38, h * 0.26, w * 0.62, h * 0.74)
            break
        case "duplicate":
        case "copy":
            rect(ctx, w * 0.30, h * 0.22, w * 0.50, h * 0.54, false)
            line(ctx, w * 0.20, h * 0.34, w * 0.20, h * 0.84)
            line(ctx, w * 0.20, h * 0.84, w * 0.68, h * 0.84)
            break
        case "snap":
            path(ctx, [["M", 5, 4], ["L", 5, 13], ["C", 5, 22.5, 19, 22.5, 19, 13], ["L", 19, 4]], false)
            line(ctx, w * 0.21, h * 0.37, w * 0.21, h * 0.18)
            line(ctx, w * 0.79, h * 0.37, w * 0.79, h * 0.18)
            line(ctx, w * 0.22, h * 0.38, w * 0.35, h * 0.38)
            line(ctx, w * 0.65, h * 0.38, w * 0.78, h * 0.38)
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
            path(ctx, [["M", 4, 16], ["L", 4, 5], ["Q", 4, 3.5, 5.5, 3.5], ["L", 16, 3.5]], false)
            rect(ctx, w * 0.31, h * 0.31, w * 0.54, h * 0.54, false)
            path(ctx, [["M", 9, 18], ["L", 13, 13.5], ["L", 18, 18]], false)
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
            path(ctx, [["M", 4, 20], ["L", 5, 14.5], ["L", 16.5, 3.5],
                       ["Q", 17.5, 2.5, 18.5, 3.5], ["L", 20.5, 5.5],
                       ["Q", 21.5, 6.5, 20.5, 7.5], ["L", 9.5, 19], ["Z"]], false)
            line(ctx, w * 0.61, h * 0.23, w * 0.78, h * 0.40)
            break
        case "masks":
            rect(ctx, w * 0.17, h * 0.17, w * 0.66, h * 0.66, false)
            circle(ctx, midX, midY, w * 0.17, true)
            break
        case "layers":
            poly(ctx, [[midX, h * 0.17], [w * 0.85, h * 0.38], [midX, h * 0.59], [w * 0.15, h * 0.38]], false)
            path(ctx, [["M", 3.6, 15], ["L", 12, 20], ["L", 20.4, 15]], false)
            break
        case "group":
            rect(ctx, w * 0.16, h * 0.18, w * 0.40, h * 0.40, false)
            path(ctx, [["M", 15, 10], ["L", 20, 10], ["L", 20, 20], ["L", 10, 20], ["L", 10, 15]], false)
            break
        case "carousel":
            rect(ctx, w * 0.30, h * 0.17, w * 0.40, h * 0.66, false)
            path(ctx, [["M", 4, 6], ["L", 3, 6], ["L", 3, 18], ["L", 4, 18]], false)
            path(ctx, [["M", 20, 6], ["L", 21, 6], ["L", 21, 18], ["L", 20, 18]], false)
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
            rect(ctx, w * 0.17, h * 0.20, w * 0.66, h * 0.48, false)
            line(ctx, midX, h * 0.69, midX, h * 0.84)
            line(ctx, w * 0.34, h * 0.84, w * 0.66, h * 0.84)
            poly(ctx, [[w * 0.43, h * 0.33], [w * 0.43, h * 0.55], [w * 0.61, h * 0.44]], true)
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
            path(ctx, [["M", 8, 4], ["L", 4, 4], ["L", 4, 8]], false)
            path(ctx, [["M", 16, 4], ["L", 20, 4], ["L", 20, 8]], false)
            path(ctx, [["M", 4, 16], ["L", 4, 20], ["L", 8, 20]], false)
            path(ctx, [["M", 20, 16], ["L", 20, 20], ["L", 16, 20]], false)
            rect(ctx, w * 0.32, h * 0.36, w * 0.36, h * 0.28, false)
            break
        case "fill":
            rect(ctx, w * 0.20, h * 0.20, w * 0.60, h * 0.60, false)
            path(ctx, [["M", 8, 16], ["L", 16, 8], ["M", 11, 8], ["L", 16, 8], ["L", 16, 13]], false)
            break
        case "zoom":
            circle(ctx, w * 0.42, h * 0.42, w * 0.22, false)
            line(ctx, w * 0.58, h * 0.58, w * 0.80, h * 0.80)
            line(ctx, w * 0.34, h * 0.42, w * 0.50, h * 0.42)
            line(ctx, w * 0.42, h * 0.34, w * 0.42, h * 0.50)
            break
        case "crop":
            path(ctx, [["M", 6, 3], ["L", 6, 18], ["L", 21, 18]], false)
            path(ctx, [["M", 3, 6], ["L", 18, 6], ["L", 18, 21]], false)
            break
        case "local-adjust":
            circle(ctx, midX, midY, w * 0.33, false)
            line(ctx, w * 0.30, midY, w * 0.70, midY)
            circle(ctx, w * 0.44, midY, w * 0.06, true)
            break
        case "retouch":
            path(ctx, [["M", 4.5, 13], ["L", 13, 4.5], ["Q", 15, 2.5, 17, 4.5],
                       ["L", 19.5, 7], ["Q", 21.5, 9, 19.5, 11], ["L", 11, 19.5],
                       ["Q", 9, 21.5, 7, 19.5], ["L", 4.5, 17], ["Q", 2.5, 15, 4.5, 13], ["Z"]], false)
            line(ctx, w * 0.36, h * 0.40, w * 0.60, h * 0.64)
            line(ctx, w * 0.43, h * 0.33, w * 0.67, h * 0.57)
            break
        case "geometry":
            rect(ctx, w * 0.24, h * 0.24, w * 0.52, h * 0.52, false)
            for (const x of [0.24, 0.76])
                for (const y of [0.24, 0.76]) circle(ctx, w * x, h * y, w * 0.055, true)
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
        case "camera-lens":
            circle(ctx, midX, midY, w * 0.35, false)
            circle(ctx, midX, midY, w * 0.22, false)
            path(ctx, [["M", 9, 10], ["Q", 9.5, 8, 12, 8]], false)
            break
        case "aperture":
            circle(ctx, midX, midY, w * 0.36, false)
            path(ctx, [["M", 10.00, 8.54], ["L", 19.92, 8.54], ["M", 14.00, 8.54], ["L", 18.96, 17.12],
                       ["M", 16.00, 12.00], ["L", 11.04, 20.59], ["M", 14.00, 15.46], ["L", 4.08, 15.46],
                       ["M", 10.00, 15.46], ["L", 5.04, 6.88], ["M", 8.00, 12.00], ["L", 12.96, 3.41]], false)
            break
        case "exposure":
            circle(ctx, midX, midY, w * 0.21, false)
            line(ctx, midX, h * 0.08, midX, h * 0.20)
            line(ctx, midX, h * 0.80, midX, h * 0.92)
            line(ctx, w * 0.08, midY, w * 0.20, midY)
            line(ctx, w * 0.80, midY, w * 0.92, midY)
            line(ctx, w * 0.20, h * 0.20, w * 0.29, h * 0.29)
            line(ctx, w * 0.71, h * 0.71, w * 0.80, h * 0.80)
            line(ctx, w * 0.20, h * 0.80, w * 0.29, h * 0.71)
            line(ctx, w * 0.71, h * 0.29, w * 0.80, h * 0.20)
            break
        case "orbit":
            ctx.save()
            ctx.translate(midX, midY)
            ctx.rotate(-Math.PI / 6)
            ctx.beginPath()
            ctx.ellipse(-w * 0.36, -h * 0.20, w * 0.72, h * 0.40)
            ctx.stroke()
            ctx.restore()
            circle(ctx, midX, midY, w * 0.075, true)
            break
        case "pan":
            line(ctx, midX, h * 0.17, midX, h * 0.83)
            line(ctx, w * 0.17, midY, w * 0.83, midY)
            poly(ctx, [[midX, h * 0.12], [w * 0.40, h * 0.28], [w * 0.60, h * 0.28]], true)
            poly(ctx, [[midX, h * 0.88], [w * 0.40, h * 0.72], [w * 0.60, h * 0.72]], true)
            poly(ctx, [[w * 0.12, midY], [w * 0.28, h * 0.40], [w * 0.28, h * 0.60]], true)
            poly(ctx, [[w * 0.88, midY], [w * 0.72, h * 0.40], [w * 0.72, h * 0.60]], true)
            break
        case "arrow-left":
            line(ctx, w * 0.82, midY, w * 0.20, midY)
            line(ctx, w * 0.20, midY, w * 0.43, h * 0.27)
            line(ctx, w * 0.20, midY, w * 0.43, h * 0.73)
            break
        case "arrow-right":
            line(ctx, w * 0.18, midY, w * 0.80, midY)
            line(ctx, w * 0.80, midY, w * 0.57, h * 0.27)
            line(ctx, w * 0.80, midY, w * 0.57, h * 0.73)
            break
        case "arrow-up":
            line(ctx, midX, h * 0.82, midX, h * 0.20)
            line(ctx, midX, h * 0.20, w * 0.27, h * 0.43)
            line(ctx, midX, h * 0.20, w * 0.73, h * 0.43)
            break
        case "arrow-down":
            line(ctx, midX, h * 0.18, midX, h * 0.80)
            line(ctx, midX, h * 0.80, w * 0.27, h * 0.57)
            line(ctx, midX, h * 0.80, w * 0.73, h * 0.57)
            break
        case "zoom-in":
            circle(ctx, w * 0.43, h * 0.43, w * 0.25, false)
            line(ctx, w * 0.60, h * 0.60, w * 0.84, h * 0.84)
            line(ctx, w * 0.43, h * 0.32, w * 0.43, h * 0.54)
            line(ctx, w * 0.32, h * 0.43, w * 0.54, h * 0.43)
            break
        case "zoom-out":
            circle(ctx, w * 0.43, h * 0.43, w * 0.25, false)
            line(ctx, w * 0.60, h * 0.60, w * 0.84, h * 0.84)
            line(ctx, w * 0.32, h * 0.43, w * 0.54, h * 0.43)
            break
        case "film":
            rect(ctx, w * 0.18, h * 0.17, w * 0.64, h * 0.66, false)
            line(ctx, w * 0.33, h * 0.19, w * 0.33, h * 0.81)
            line(ctx, w * 0.67, h * 0.19, w * 0.67, h * 0.81)
            for (const y of [0.33, 0.67]) {
                circle(ctx, w * 0.25, h * y, w * 0.025, true)
                circle(ctx, w * 0.75, h * y, w * 0.025, true)
            }
            break
        case "sparkles":
            sparkle(ctx, 10, 10, 7)
            sparkle(ctx, 18.5, 18, 3)
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
            rect(ctx, w * 0.16, h * 0.20, w * 0.68, h * 0.60, false)
            line(ctx, midX, h * 0.12, midX, h * 0.88)
            break
        case "visible":
            path(ctx, [["M", 3, 12], ["C", 7, 4.5, 17, 4.5, 21, 12],
                       ["C", 17, 19.5, 7, 19.5, 3, 12], ["Z"]], false)
            circle(ctx, midX, midY, w * 0.12, false)
            break
        case "hidden":
            path(ctx, [["M", 3, 12], ["Q", 5, 8.5, 8, 7.5],
                       ["M", 11, 6.5], ["Q", 17.5, 6, 21, 12], ["Q", 19, 15.5, 16, 16.5],
                       ["M", 13, 17.5], ["Q", 6.5, 18, 3, 12]], false)
            line(ctx, w * 0.20, h * 0.82, w * 0.80, h * 0.18)
            break
        case "panel-left":
            rect(ctx, w * 0.16, h * 0.20, w * 0.68, h * 0.60, false)
            line(ctx, w * 0.40, h * 0.22, w * 0.40, h * 0.78)
            break
        case "panel-right":
            rect(ctx, w * 0.16, h * 0.20, w * 0.68, h * 0.60, false)
            line(ctx, w * 0.60, h * 0.22, w * 0.60, h * 0.78)
            break
        case "panel-bottom":
            rect(ctx, w * 0.16, h * 0.20, w * 0.68, h * 0.60, false)
            line(ctx, w * 0.18, h * 0.60, w * 0.82, h * 0.60)
            break
        case "density":
            line(ctx, w * 0.22, h * 0.28, w * 0.78, h * 0.28)
            line(ctx, w * 0.22, midY, w * 0.78, midY)
            line(ctx, w * 0.22, h * 0.72, w * 0.78, h * 0.72)
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
            path(ctx, [["M", 4, 19], ["L", 5, 15], ["L", 14, 6], ["L", 18, 10],
                       ["L", 9, 19], ["Z"], ["M", 11, 5], ["L", 19, 13]], false)
            path(ctx, [["M", 15, 6], ["L", 17, 4], ["Q", 19, 2, 21, 4],
                       ["Q", 23, 6, 21, 8], ["L", 19, 10]], false)
            break
        case "agent-terminal":
            rect(ctx, w * 0.14, h * 0.20, w * 0.72, h * 0.60, false)
            path(ctx, [["M", 7, 9], ["L", 10, 12], ["L", 7, 15]], false)
            line(ctx, w * 0.54, h * 0.63, w * 0.72, h * 0.63)
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
            path(ctx, [["M", 8, 10.5], ["L", 8, 7.5], ["C", 8, 1.5, 17, 1.5, 17, 7]], false)
            line(ctx, midX, h * 0.58, midX, h * 0.68)
            break
        case "stop":
            rect(ctx, w * 0.26, h * 0.26, w * 0.48, h * 0.48, true)
            break
        case "record":
            circle(ctx, midX, midY, w * 0.29, true)
            break
        case "presets":
            path(ctx, [["M", 12, 6], ["L", 5, 6], ["Q", 3, 6, 3, 8],
                       ["L", 3, 18], ["Q", 3, 20, 5, 20], ["L", 17, 20],
                       ["Q", 19, 20, 19, 18], ["L", 19, 12]], false)
            path(ctx, [["M", 5.5, 17], ["L", 10, 12.5], ["L", 14.5, 17]], false)
            sparkle(ctx, 18, 6, 4)
            break
        case "subject":
            circle(ctx, midX, h * 0.32, w * 0.15, false)
            path(ctx, [["M", 4.5, 20], ["C", 4.5, 11, 19.5, 11, 19.5, 20]], false)
            break
        case "rectangle":
            rect(ctx, w * 0.17, h * 0.24, w * 0.66, h * 0.52, false)
            break
        case "ellipse":
            ctx.beginPath()
            ctx.ellipse(w * 0.17, h * 0.24, w * 0.66, h * 0.52)
            ctx.stroke()
            break
        case "triangle":
            poly(ctx, [[midX, h * 0.18], [w * 0.84, h * 0.80], [w * 0.16, h * 0.80]], false)
            break
        case "hexagon":
            poly(ctx, [[w * 0.32, h * 0.19], [w * 0.68, h * 0.19], [w * 0.86, midY],
                       [w * 0.68, h * 0.81], [w * 0.32, h * 0.81], [w * 0.14, midY]], false)
            break
        case "bring-front":
            path(ctx, [["M", 6, 9], ["L", 4, 9], ["L", 4, 20], ["L", 15, 20], ["L", 15, 18]], false)
            rect(ctx, w * 0.37, h * 0.16, w * 0.47, h * 0.47, true)
            break
        case "send-back":
            rect(ctx, w * 0.16, h * 0.37, w * 0.47, h * 0.47, true)
            path(ctx, [["M", 9, 6], ["L", 9, 4], ["L", 20, 4], ["L", 20, 15], ["L", 18, 15]], false)
            break
        case "gradient-linear": {
            const gradient = ctx.createLinearGradient(w * 0.20, 0, w * 0.80, 0)
            gradient.addColorStop(0, Qt.rgba(root.color.r, root.color.g, root.color.b, 0.12))
            gradient.addColorStop(1, root.color)
            ctx.fillStyle = gradient
            rect(ctx, w * 0.17, h * 0.24, w * 0.66, h * 0.52, true)
            break
        }
        case "gradient-radial": {
            const gradient = ctx.createRadialGradient(midX, midY, 0, midX, midY, w * 0.34)
            gradient.addColorStop(0, root.color)
            gradient.addColorStop(1, Qt.rgba(root.color.r, root.color.g, root.color.b, 0.12))
            ctx.fillStyle = gradient
            circle(ctx, midX, midY, w * 0.34, true)
            break
        }
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
