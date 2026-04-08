import QtQuick
import QtQuick.Controls
import PhotoEditor
import "../components"

Item {
    id: root

    required property var editRoot
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property real hoverX: 0
    property real hoverY: 0
    property bool hoverActive: false
    property var smartSelectStrokePoints: []
    property bool smartSelectStrokeActive: false

    visible: imageWidth > 0 && imageHeight > 0

    function previewPointX(normalizedX) {
        return normalizedX * previewItem.width
    }

    function previewPointY(normalizedY) {
        return normalizedY * previewItem.height
    }

    function normalizedStrokePoint(mouseX, mouseY) {
        return {
            x: Math.max(0, Math.min(1, previewItem.width > 0 ? mouseX / previewItem.width : 0.5)),
            y: Math.max(0, Math.min(1, previewItem.height > 0 ? mouseY / previewItem.height : 0.5))
        }
    }

    function resetSmartSelectStroke() {
        smartSelectStrokePoints = []
        smartSelectStrokeActive = false
    }

    function appendSmartSelectStrokePoint(mouseX, mouseY, force) {
        const point = normalizedStrokePoint(mouseX, mouseY)
        const points = smartSelectStrokePoints
        if (!force && points.length > 0) {
            const lastPoint = points[points.length - 1]
            const deltaX = (point.x - lastPoint.x) * previewItem.width
            const deltaY = (point.y - lastPoint.y) * previewItem.height
            if ((deltaX * deltaX) + (deltaY * deltaY) < 81)
                return false
        }
        smartSelectStrokePoints = points.concat([point])
        return true
    }

    function beginSmartSelectStroke(mouseX, mouseY) {
        smartSelectStrokeActive = true
        smartSelectStrokePoints = []
        appendSmartSelectStrokePoint(mouseX, mouseY, true)
        editorPreview.scheduleSmartSelectPreviewPath(smartSelectStrokePoints)
    }

    function updateSmartSelectStroke(mouseX, mouseY) {
        if (!smartSelectStrokeActive)
            return
        if (appendSmartSelectStrokePoint(mouseX, mouseY, false))
            editorPreview.scheduleSmartSelectPreviewPath(smartSelectStrokePoints)
    }

    function commitSmartSelectStroke() {
        if (!smartSelectStrokeActive || smartSelectStrokePoints.length === 0) {
            resetSmartSelectStroke()
            return
        }
        const outcome = editorPreview.pickMaskPath(
                    smartSelectStrokePoints,
                    editRoot.objectPickMode,
                    editRoot.objectPickCreateNew)
        resetSmartSelectStroke()
        editRoot.handleMaskPickOutcome(outcome)
    }

    function activeSelectionSetId() {
        if (!editorPreview.hasActiveMask ||
                editorPreview.activeMaskToolRole === "retouch" ||
                editorPreview.activeMaskIndex < 0 ||
                editorPreview.activeMaskIndex >= editorPreview.maskStack.length)
            return ""
        const activeMask = editorPreview.maskStack[editorPreview.activeMaskIndex]
        return activeMask && activeMask.maskSetId ? activeMask.maskSetId : ""
    }

    function objectPickAccentColor() {
        if (editRoot.objectPickMode === "remove")
            return "#d98962"
        if (editRoot.objectPickMode === "selection-subtract")
            return "#d36a5e"
        return "#5e95d3"
    }

    function objectPickActionLabel() {
        if (editRoot.objectPickMode === "remove")
            return editRoot.objectPickCreateNew ? "New Remove Target" : "Remove Object"
        if (editRoot.objectPickMode === "selection-subtract")
            return "Subtract Object"
        return editRoot.objectPickCreateNew ? "New Object Selection" : "Add Object"
    }

    function objectPickInstructionText() {
        const previewReady = editorPreview.smartSelectPreviewActive
        if (editRoot.objectPickMode === "remove") {
            return previewReady
                    ? "Release to stage the highlighted object for removal."
                    : (editRoot.objectPickCreateNew
                       ? "Hover an object, then click or drag to start a new removal target."
                       : "Hover an object, then click or drag to add it to the staged removal.")
        }
        if (editRoot.objectPickMode === "selection-subtract") {
            return previewReady
                    ? "Release to subtract the highlighted object from the selection."
                    : "Hover an object, then click or drag to subtract it from the selection."
        }
        return previewReady
                ? (editRoot.objectPickCreateNew
                   ? "Release to create a new selection from the highlighted object."
                   : "Release to add the highlighted object to the current selection.")
                : (editRoot.objectPickCreateNew
                   ? "Hover an object, then click or drag to create a separate selection."
                   : "Hover an object, then click or drag to add it to the current selection.")
    }

    function overlayOpacity(mask, active, sameSet) {
        if (active)
            return 0.96
        if (!editorPreview.hasActiveMask)
            return 0.58
        if (sameSet)
            return 0.0
        if (mask && mask.toolRole === "retouch")
            return 0.0
        return 0.0
    }

    function overlayVisible(mask, active, sameSet) {
        if (!mask || !mask.enabled)
            return false
        if (!editorPreview.hasActiveMask)
            return false
        return active
    }

    Item {
        id: previewItem
        x: root.imageX
        y: root.imageY
        width: root.imageWidth
        height: root.imageHeight
        visible: root.visible

        Repeater {
            model: editorPreview.maskStack

            delegate: Item {
                required property int index
                required property var modelData

                readonly property var mask: modelData
                readonly property bool active: index === editorPreview.activeMaskIndex
                readonly property string activeSetId: root.activeSelectionSetId()
                readonly property bool sameSet: mask.toolRole !== "retouch" &&
                                                activeSetId.length > 0 &&
                                                mask.maskSetId === activeSetId
                readonly property color accent: editRoot.maskAccent(mask)
                readonly property real emphasis: root.overlayOpacity(mask, active, sameSet)

                anchors.fill: parent
                visible: root.overlayVisible(mask, active, sameSet)

                Item {
                    id: linearOverlay
                    visible: mask.kind === "linear"
                    readonly property real positionValue: mask && mask.position !== undefined ? mask.position : 0.5
                    readonly property real featherValue: mask && mask.feather !== undefined ? mask.feather : 0.22
                    readonly property real rotationValue: mask && mask.rotation !== undefined ? mask.rotation : 0
                    anchors.centerIn: parent
                    width: parent.width * 1.35
                    height: parent.height * 1.35
                    rotation: rotationValue
                    opacity: emphasis

                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Qt.alpha(accent, 0.36) }
                            GradientStop {
                                position: Math.max(0.0, linearOverlay.positionValue - linearOverlay.featherValue * 0.5)
                                color: Qt.alpha(accent, 0.16)
                            }
                            GradientStop { position: linearOverlay.positionValue; color: Qt.rgba(0.95, 0.97, 0.98, 0.74) }
                            GradientStop {
                                position: Math.min(1.0, linearOverlay.positionValue + linearOverlay.featherValue * 0.5)
                                color: Qt.alpha(accent, 0.08)
                            }
                            GradientStop { position: 1.0; color: Qt.alpha(accent, 0.0) }
                        }
                    }

                    Rectangle {
                        x: 0
                        y: Math.max(0, Math.min(linearOverlay.height - 1, linearOverlay.height * linearOverlay.positionValue))
                        width: linearOverlay.width
                        height: 1
                        color: "#f4f7fa"
                        opacity: 0.92
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: active ? Qt.CrossCursor : Qt.PointingHandCursor
                        onPressed: function(mouse) {
                            editorPreview.activeMaskIndex = index
                            if (active)
                                editorPreview.activeMaskPosition = editRoot.clamp(mouse.y / linearOverlay.height, 0, 1)
                        }
                    }

                    Rectangle {
                        visible: active
                        x: linearOverlay.width / 2 - width / 2
                        y: linearOverlay.height * linearOverlay.positionValue - height / 2
                        width: 16
                        height: 16
                        radius: 8
                        color: "#f4f7fa"
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeVerCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskPosition = editRoot.clamp(
                                            (parent.y + mouse.y + parent.height / 2) / linearOverlay.height,
                                            0,
                                            1)
                            }
                        }
                    }

                    Rectangle {
                        visible: active
                        x: linearOverlay.width / 2 - width / 2
                        y: -28
                        width: 14
                        height: 14
                        radius: 7
                        color: accent
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.CrossCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                const dx = mouse.x + parent.x - linearOverlay.width / 2
                                const dy = mouse.y + parent.y - linearOverlay.height / 2
                                editorPreview.activeMaskRotation = Math.atan2(dy, dx) * 180 / Math.PI + 90
                            }
                        }
                    }
                }

                Item {
                    id: radialOverlay
                    visible: mask.kind === "radial"
                    readonly property real centerXValue: mask && mask.centerX !== undefined ? mask.centerX : 0.5
                    readonly property real centerYValue: mask && mask.centerY !== undefined ? mask.centerY : 0.5
                    readonly property real radiusXValue: mask && mask.radiusX !== undefined ? mask.radiusX : 0.2
                    readonly property real radiusYValue: mask && mask.radiusY !== undefined ? mask.radiusY : 0.2
                    readonly property real featherValue: mask && mask.feather !== undefined ? mask.feather : 0.25
                    readonly property real rotationValue: mask && mask.rotation !== undefined ? mask.rotation : 0
                    x: centerXValue * parent.width - width / 2
                    y: centerYValue * parent.height - height / 2
                    width: Math.max(24, parent.width * radiusXValue * 2)
                    height: Math.max(24, parent.height * radiusYValue * 2)
                    rotation: rotationValue
                    opacity: emphasis

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: active ? Qt.SizeAllCursor : Qt.PointingHandCursor
                        onPressed: editorPreview.activeMaskIndex = index
                    }

                    Canvas {
                        id: radialCanvas
                        anchors.fill: parent

                        Connections {
                            target: editorPreview

                            function onMaskOverlayChanged() {
                                radialCanvas.requestPaint()
                            }
                        }

                        onPaint: {
                            const ctx = getContext("2d")
                            const innerScale = Math.max(0.0, 1.0 - radialOverlay.featherValue)
                            ctx.reset()
                            ctx.strokeStyle = "#f4f7fa"
                            ctx.lineWidth = active ? 1.8 : 1.2
                            ctx.fillStyle = Qt.alpha(accent, active ? 0.20 : 0.11)
                            ctx.beginPath()
                            ctx.ellipse(width / 2, height / 2, Math.max(1, width / 2 - 1), Math.max(1, height / 2 - 1), 0, 0, Math.PI * 2)
                            ctx.fill()
                            ctx.stroke()
                            ctx.strokeStyle = "rgba(244,247,250,0.45)"
                            ctx.beginPath()
                            ctx.ellipse(width / 2, height / 2,
                                        Math.max(1, (width / 2 - 1) * innerScale),
                                        Math.max(1, (height / 2 - 1) * innerScale),
                                        0, 0, Math.PI * 2)
                            ctx.stroke()
                        }
                    }

                    Rectangle {
                        visible: active
                        x: radialOverlay.width / 2 - width / 2
                        y: radialOverlay.height / 2 - height / 2
                        width: 16
                        height: 16
                        radius: 8
                        color: "#f4f7fa"
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeAllCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskCenterX = editRoot.clamp((radialOverlay.x + parent.x + mouse.x) / previewItem.width, 0, 1)
                                editorPreview.activeMaskCenterY = editRoot.clamp((radialOverlay.y + parent.y + mouse.y) / previewItem.height, 0, 1)
                            }
                        }
                    }

                    Rectangle {
                        visible: active
                        x: radialOverlay.width - width / 2
                        y: radialOverlay.height / 2 - height / 2
                        width: 14
                        height: 14
                        radius: 7
                        color: accent
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeHorCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskRadiusX = editRoot.clamp((parent.x + mouse.x) / previewItem.width / 2, 0.05, 1.0)
                            }
                        }
                    }

                    Rectangle {
                        visible: active
                        x: radialOverlay.width / 2 - width / 2
                        y: radialOverlay.height - height / 2
                        width: 14
                        height: 14
                        radius: 7
                        color: accent
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeVerCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                editorPreview.activeMaskRadiusY = editRoot.clamp((parent.y + mouse.y) / previewItem.height / 2, 0.05, 1.0)
                            }
                        }
                    }

                    Rectangle {
                        visible: active
                        x: radialOverlay.width / 2 - width / 2
                        y: -30
                        width: 14
                        height: 14
                        radius: 7
                        color: "#f0c46e"
                        border.width: 1
                        border.color: "#101419"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.CrossCursor
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                const dx = mouse.x + parent.x - radialOverlay.width / 2
                                const dy = mouse.y + parent.y - radialOverlay.height / 2
                                editorPreview.activeMaskRotation = Math.atan2(dy, dx) * 180 / Math.PI + 90
                            }
                        }
                    }
                }

                Item {
                    id: brushOverlay
                    visible: mask.kind === "brush"
                    readonly property bool showStrokePoints: (active && editorPreview.brushStrokeActive) ||
                                                             mask.retouchMode === "clone" ||
                                                             editorPreview.activeMaskPreviewSource.length === 0
                    anchors.fill: parent
                    opacity: emphasis
                    z: active && editorPreview.brushStrokeActive ? 98 : 0

                    Canvas {
                        id: brushCanvas
                        anchors.fill: parent
                        visible: brushOverlay.showStrokePoints &&
                                 !(active && editorPreview.brushStrokeActive &&
                                   editorPreview.activeMaskLiveBrushPreviewSource.length > 0)

                        function pointX(point) {
                            return (point.x || 0.5) * brushOverlay.width
                        }

                        function pointY(point) {
                            return (point.y || 0.5) * brushOverlay.height
                        }

                        function pointDiameter(point) {
                            return Math.max(8, Math.min(previewItem.width, previewItem.height) * (point.size || mask.brushSize || 0.08) * 2)
                        }

                        function pointAlpha(point) {
                            return Math.max(0.14, (point.flow || mask.brushFlow || 1.0) * 0.34)
                        }

                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()

                            const points = active && editorPreview.brushStrokeActive
                                           ? (editorPreview.activeMaskLiveBrushPoints || [])
                                           : (brushOverlay.showStrokePoints ? (mask.brushPoints || []) : [])
                            if (points.length === 0)
                                return

                            const first = points[0]
                            ctx.fillStyle = Qt.alpha(accent, pointAlpha(first))
                            ctx.beginPath()
                            ctx.arc(pointX(first), pointY(first), pointDiameter(first) / 2, 0, Math.PI * 2)
                            ctx.fill()

                            for (let i = 1; i < points.length; ++i) {
                                const previous = points[i - 1]
                                const current = points[i]
                                const strokeDiameter = (pointDiameter(previous) + pointDiameter(current)) / 2

                                ctx.strokeStyle = Qt.alpha(accent, Math.max(pointAlpha(previous), pointAlpha(current)))
                                ctx.lineCap = "round"
                                ctx.lineJoin = "round"
                                ctx.lineWidth = strokeDiameter
                                ctx.beginPath()
                                ctx.moveTo(pointX(previous), pointY(previous))
                                ctx.lineTo(pointX(current), pointY(current))
                                ctx.stroke()

                                ctx.fillStyle = Qt.alpha(accent, pointAlpha(current))
                                ctx.beginPath()
                                ctx.arc(pointX(current), pointY(current), pointDiameter(current) / 2, 0, Math.PI * 2)
                                ctx.fill()
                            }
                        }

                        onVisibleChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()

                        Connections {
                            target: editorPreview

                            function onMaskOverlayChanged() {
                                if (brushCanvas.visible)
                                    brushCanvas.requestPaint()
                            }

                            function onActiveBrushStrokeChanged() {
                                if (brushCanvas.visible)
                                    brushCanvas.requestPaint()
                            }
                        }
                    }

                    Image {
                        anchors.fill: parent
                        visible: active &&
                                 editorPreview.brushStrokeActive &&
                                 !editorPreview.activeMaskLiveBrushPreviewNativeAvailable &&
                                 editorPreview.activeMaskLiveBrushPreviewSource.length > 0
                        source: editorPreview.activeMaskLiveBrushPreviewSource
                        fillMode: Image.Stretch
                        smooth: true
                        mipmap: false
                        cache: false
                    }

                    BrushLivePreviewItem {
                        anchors.fill: parent
                        visible: active &&
                                 editorPreview.brushStrokeActive &&
                                 editorPreview.activeMaskLiveBrushPreviewNativeAvailable
                        controller: editorPreview
                    }

                    Item {
                        visible: active && mask.retouchMode === "clone" && (mask.cloneSourceSet || false)
                        x: (mask.cloneSourceX || 0.5) * brushOverlay.width - width / 2
                        y: (mask.cloneSourceY || 0.5) * brushOverlay.height - height / 2
                        width: 22
                        height: 22

                        Rectangle {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            radius: 9
                            color: "transparent"
                            border.width: 1.5
                            border.color: "#f4f7fa"
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 2
                            height: 22
                            radius: 1
                            color: "#f4f7fa"
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: 22
                            height: 2
                            radius: 1
                            color: "#f4f7fa"
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        enabled: active
                        hoverEnabled: true
                        preventStealing: true
                        cursorShape: Qt.CrossCursor
                        onEntered: root.hoverActive = true
                        onExited: root.hoverActive = false
                        onPositionChanged: function(mouse) {
                            root.hoverX = mouse.x
                            root.hoverY = mouse.y
                            root.hoverActive = true
                            if (!pressed || editRoot.cloneSourcePickArmed ||
                                    (mask.retouchMode === "clone" && !(mask.cloneSourceSet || false)))
                                return
                            editorPreview.addBrushPoint(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height)
                        }
                        onPressed: function(mouse) {
                            if (editRoot.maskFallbackMode === "brush")
                                editRoot.maskFallbackMode = ""
                            editorPreview.activeMaskIndex = index
                            editorPreview.beginActiveBrushStroke()
                            root.hoverX = mouse.x
                            root.hoverY = mouse.y
                            root.hoverActive = true
                            if (mask.retouchMode === "clone" &&
                                    (editRoot.cloneSourcePickArmed || !(mask.cloneSourceSet || false))) {
                                editorPreview.setActiveMaskCloneSource(
                                            mouse.x / brushOverlay.width,
                                            mouse.y / brushOverlay.height)
                                editRoot.clearTransientTools()
                                return
                            }
                            editorPreview.addBrushPoint(mouse.x / brushOverlay.width, mouse.y / brushOverlay.height)
                        }
                        onReleased: editorPreview.endActiveBrushStroke()
                        onCanceled: editorPreview.endActiveBrushStroke()
                    }
                }

                Item {
                    id: polygonOverlay
                    visible: mask.kind === "polygon"
                    anchors.fill: parent
                    opacity: emphasis

                    Canvas {
                        id: polygonCanvas
                        anchors.fill: parent

                        Connections {
                            target: editorPreview

                            function onMaskOverlayChanged() {
                                polygonCanvas.requestPaint()
                            }
                        }

                        onPaint: {
                            const points = mask.polygonPoints || []
                            const count = points.length
                            const ctx = getContext("2d")
                            ctx.reset()
                            if (count === 0)
                                return

                            ctx.beginPath()
                            ctx.moveTo(root.previewPointX(points[0].x || 0.5), root.previewPointY(points[0].y || 0.5))
                            for (let i = 1; i < count; ++i)
                                ctx.lineTo(root.previewPointX(points[i].x || 0.5), root.previewPointY(points[i].y || 0.5))
                            if (count >= 3)
                                ctx.closePath()

                            if (count >= 3) {
                                ctx.fillStyle = Qt.alpha(accent, active ? 0.18 : 0.09)
                                ctx.fill()
                            }

                            ctx.strokeStyle = Qt.alpha("#f4f7fa", active ? 0.88 : 0.55)
                            ctx.lineWidth = active ? 1.8 : 1.1
                            if (ctx.setLineDash)
                                ctx.setLineDash([8, 6])
                            ctx.lineDashOffset = -editRoot.selectionOutlinePhase
                            ctx.stroke()
                        }
                    }

                    Repeater {
                        model: mask.polygonPoints || []

                        delegate: Rectangle {
                            required property var modelData

                            width: 10
                            height: 10
                            radius: 5
                            x: root.previewPointX(modelData.x || 0.5) - width / 2
                            y: root.previewPointY(modelData.y || 0.5) - height / 2
                            color: active ? "#f4f7fa" : accent
                            border.width: 1
                            border.color: "#101419"
                            visible: active
                        }
                    }
                }

                Rectangle {
                    visible: active &&
                             (mask.kind === "luminance" ||
                              mask.kind === "color")
                    anchors.centerIn: parent
                    radius: 10
                    color: Qt.alpha(accent, 0.18)
                    border.width: 1
                    border.color: Qt.alpha("#f4f7fa", 0.35)
                    implicitWidth: rangeLabel.implicitWidth + 24
                    implicitHeight: rangeLabel.implicitHeight + 14

                    Label {
                        id: rangeLabel
                        anchors.centerIn: parent
                        text: mask.kind === "luminance"
                              ? "Luminance range active"
                              : mask.kind === "color"
                                ? "Color range active"
                                : ""
                        color: "#f2f4f6"
                        font.family: "Avenir Next"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: true
            enabled: editRoot.objectPickArmed ||
                     (editorPreview.hasActiveMask &&
                      editorPreview.activeMaskKind === "brush" &&
                      !editRoot.objectPickArmed &&
                      !editRoot.polygonDrawArmed &&
                      !editRoot.lassoDrawArmed)
            z: 95

            onEntered: root.hoverActive = true
            onExited: root.hoverActive = false
            onPositionChanged: function(mouse) {
                root.hoverX = mouse.x
                root.hoverY = mouse.y
                root.hoverActive = true
            }
        }

        Rectangle {
            visible: ((editorPreview.hasActiveMask &&
                       editorPreview.activeMaskKind === "brush" &&
                       !editRoot.objectPickArmed &&
                       !editRoot.polygonDrawArmed &&
                       !editRoot.lassoDrawArmed) ||
                      editRoot.objectPickArmed) &&
                     root.hoverActive
            x: root.hoverX - width / 2
            y: root.hoverY - height / 2
            width: editRoot.objectPickArmed
                   ? 42
                   : Math.max(8, Math.min(previewItem.width, previewItem.height) * editorPreview.activeMaskBrushSize * 2)
            height: width
            radius: width / 2
            z: 98
            color: Qt.alpha(
                       editRoot.objectPickArmed
                       ? root.objectPickAccentColor()
                       : "#f4f7fa",
                       editRoot.objectPickArmed ? 0.08 : 0.04)
            border.width: 1
            border.color: editRoot.objectPickArmed
                          ? root.objectPickAccentColor()
                          : (editorPreview.activeMaskRetouchMode === "remove"
                             ? "#d98962"
                             : editorPreview.activeMaskRetouchMode === "clone"
                               ? "#7cb5ff"
                               : editorPreview.activeMaskRetouchMode === "heal"
                                 ? "#72cbb2"
                                 : (editorPreview.activeMaskOperation === "subtract" ? "#d36a5e" : "#f4f7fa"))
        }

        Image {
            id: activeSelectionMatte
            anchors.fill: parent
            visible: editorPreview.hasActiveMask &&
                     editorPreview.activeMaskPreviewSource.length > 0 &&
                     !(editorPreview.brushStrokeActive && editorPreview.activeMaskKind === "brush")
            source: editorPreview.activeMaskPreviewSource
            fillMode: Image.Stretch
            smooth: true
            mipmap: false
            cache: false
            z: 95
        }

        Canvas {
            id: activeSelectionOutline
            anchors.fill: parent
            visible: editorPreview.hasActiveMask &&
                     editorPreview.activeMaskOutlinePoints.length >= 2 &&
                     (editorPreview.activeMaskKind === "subject" ||
                      editorPreview.activeMaskKind === "object" ||
                      editorPreview.activeMaskKind === "brush" ||
                      editorPreview.activeMaskKind === "polygon")
            z: 96

            Connections {
                target: editorPreview

                function onMaskOverlayChanged() {
                    activeSelectionOutline.requestPaint()
                }
            }

            Connections {
                target: editRoot

                function onSelectionOutlinePhaseChanged() {
                    activeSelectionOutline.requestPaint()
                }
            }

            onPaint: {
                const points = editorPreview.activeMaskOutlinePoints
                const count = points.length
                const ctx = getContext("2d")
                ctx.reset()
                if (count < 2)
                    return

                const activeMask = editorPreview.activeMaskIndex >= 0
                                   ? editorPreview.maskStack[editorPreview.activeMaskIndex]
                                   : null
                const accent = editRoot.maskAccent(activeMask)
                ctx.beginPath()
                ctx.moveTo(root.previewPointX(points[0].x || 0.5), root.previewPointY(points[0].y || 0.5))
                for (let i = 1; i < count; ++i)
                    ctx.lineTo(root.previewPointX(points[i].x || 0.5), root.previewPointY(points[i].y || 0.5))
                if (count >= 3)
                    ctx.closePath()

                if (count >= 3 && editorPreview.maskPreviewMode === "overlay") {
                    ctx.fillStyle = Qt.alpha(accent, 0.12)
                    ctx.fill()
                }

                ctx.lineWidth = 2
                if (ctx.setLineDash)
                    ctx.setLineDash([8, 6])
                ctx.lineDashOffset = -editRoot.selectionOutlinePhase
                ctx.strokeStyle = "#f4f7fa"
                ctx.stroke()
                if (ctx.setLineDash)
                    ctx.setLineDash([8, 6])
                ctx.lineDashOffset = 12 - editRoot.selectionOutlinePhase
                ctx.strokeStyle = Qt.alpha("#101419", 0.78)
                ctx.stroke()
            }
        }

        Image {
            id: smartPreviewMatte
            anchors.fill: parent
            visible: editRoot.objectPickArmed &&
                     editorPreview.smartSelectPreviewActive &&
                     editorPreview.smartSelectPreviewSource.length > 0
            source: editorPreview.smartSelectPreviewSource
            fillMode: Image.Stretch
            smooth: true
            mipmap: false
            cache: false
            z: 96
        }

        Canvas {
            id: smartPreviewOutline
            anchors.fill: parent
            visible: editRoot.objectPickArmed &&
                     editorPreview.smartSelectPreviewActive &&
                     editorPreview.smartSelectPreviewOutlinePoints.length >= 2
            z: 97

            Connections {
                target: editorPreview

                function onSmartSelectPreviewChanged() {
                    smartPreviewOutline.requestPaint()
                }
            }

            Connections {
                target: editRoot

                function onSelectionOutlinePhaseChanged() {
                    smartPreviewOutline.requestPaint()
                }
            }

            onPaint: {
                const points = editorPreview.smartSelectPreviewOutlinePoints
                const count = points.length
                const ctx = getContext("2d")
                ctx.reset()
                if (count < 2)
                    return

                ctx.beginPath()
                ctx.moveTo(root.previewPointX(points[0].x || 0.5), root.previewPointY(points[0].y || 0.5))
                for (let i = 1; i < count; ++i)
                    ctx.lineTo(root.previewPointX(points[i].x || 0.5), root.previewPointY(points[i].y || 0.5))
                if (count >= 3)
                    ctx.closePath()

                if (count >= 3 && editorPreview.maskPreviewMode === "overlay") {
                    ctx.fillStyle = Qt.alpha(
                                root.objectPickAccentColor(),
                                0.18)
                    ctx.fill()
                }

                ctx.lineWidth = 2
                if (ctx.setLineDash)
                    ctx.setLineDash([10, 6])
                ctx.lineDashOffset = -editRoot.selectionOutlinePhase
                ctx.strokeStyle = "#f4f7fa"
                ctx.stroke()
                if (ctx.setLineDash)
                    ctx.setLineDash([10, 6])
                ctx.lineDashOffset = 14 - editRoot.selectionOutlinePhase
                ctx.strokeStyle = Qt.alpha(
                            root.objectPickAccentColor(),
                            0.88)
                ctx.stroke()
            }
        }

        MouseArea {
            x: 0
            y: 0
            width: previewItem.width
            height: previewItem.height
            enabled: editRoot.objectPickArmed || editRoot.polygonDrawArmed || editRoot.lassoDrawArmed
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: (editRoot.polygonDrawArmed || editRoot.lassoDrawArmed || editRoot.objectPickArmed)
                         ? Qt.CrossCursor
                         : Qt.ArrowCursor
            z: 100

            onEntered: function(mouse) {
                if (editRoot.objectPickArmed)
                    editorPreview.scheduleSmartSelectPreview(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onExited: {
                if (editRoot.objectPickArmed && !root.smartSelectStrokeActive)
                    editorPreview.clearSmartSelectPreview()
            }

            onPressed: function(mouse) {
                if (editRoot.objectPickArmed) {
                    root.beginSmartSelectStroke(mouse.x, mouse.y)
                    return
                }
                if (!editRoot.lassoDrawArmed)
                    return
                editorPreview.startPolygonSelection()
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onClicked: function(mouse) {
                if (editRoot.objectPickArmed)
                    return
                if (editRoot.lassoDrawArmed)
                    return
                if (editRoot.polygonDrawArmed) {
                    if (editRoot.maskFallbackMode === "polygon" &&
                            editorPreview.activeMaskPolygonPointCount >= 1)
                        editRoot.maskFallbackMode = ""
                    editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
                }
            }

            onPositionChanged: function(mouse) {
                if (editRoot.objectPickArmed) {
                    if (pressed)
                        root.updateSmartSelectStroke(mouse.x, mouse.y)
                    else
                        editorPreview.scheduleSmartSelectPreview(mouse.x / previewItem.width, mouse.y / previewItem.height)
                }

                if (!editRoot.lassoDrawArmed || !pressed)
                    return
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
            }

            onDoubleClicked: function(mouse) {
                if (!editRoot.polygonDrawArmed)
                    return
                editorPreview.appendActivePolygonPoint(mouse.x / previewItem.width, mouse.y / previewItem.height)
                editRoot.maskFallbackMode = ""
                editRoot.clearTransientTools()
            }

            onReleased: function(mouse) {
                if (editRoot.objectPickArmed) {
                    root.commitSmartSelectStroke()
                    if (containsMouse)
                        editorPreview.scheduleSmartSelectPreview(mouse.x / previewItem.width, mouse.y / previewItem.height)
                    return
                }
                if (!editRoot.lassoDrawArmed)
                    return
                editRoot.clearTransientTools()
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 12
            visible: editorPreview.hasActiveMask || editRoot.objectPickArmed || editRoot.polygonDrawArmed ||
                     editRoot.lassoDrawArmed || editRoot.cloneSourcePickArmed
            z: 99
            radius: 10
            color: Qt.alpha("#101419", 0.76)
            border.width: 1
            border.color: editRoot.cloneSourcePickArmed ? "#7cb5ff"
                         : root.objectPickAccentColor()
            implicitWidth: overlayInfo.implicitWidth + 20
            implicitHeight: overlayInfo.implicitHeight + 14

            Label {
                id: overlayInfo
                anchors.centerIn: parent
                text: editRoot.cloneSourcePickArmed
                      ? "Clone source"
                      : editRoot.objectPickArmed
                        ? root.objectPickActionLabel()
                        : editRoot.polygonDrawArmed
                          ? "Polygon selection"
                          : editRoot.lassoDrawArmed
                            ? "Lasso selection"
                            : editRoot.activeToolLabel()
                color: "#f2f4f6"
                font.family: "Avenir Next"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: editRoot.objectPickArmed || editRoot.polygonDrawArmed || editRoot.lassoDrawArmed ||
                     editRoot.cloneSourcePickArmed || editRoot.maskFallbackMode.length > 0
            z: 99
            color: Qt.alpha("#0b0e11", 0.16)
            border.width: 1
            border.color: editRoot.cloneSourcePickArmed ? "#7cb5ff"
                         : editRoot.maskFallbackMode === "brush" ? "#d98962"
                         : editRoot.maskFallbackMode === "polygon" ? "#7ec97a"
                         : editRoot.lassoDrawArmed ? "#7ec97a"
                         : editRoot.polygonDrawArmed ? "#7ec97a"
                         : root.objectPickAccentColor()
            radius: 12

            Label {
                anchors.centerIn: parent
                text: editRoot.cloneSourcePickArmed
                      ? "Click a clean source area for the Clone Stamp."
                      : editRoot.maskFallbackMode === "brush"
                        ? "AI could not isolate that target. Remove Brush is active so you can keep painting."
                      : editRoot.maskFallbackMode === "polygon"
                          ? "AI could not isolate that target. Continue the polygon manually and double-click to finish."
                          : editRoot.lassoDrawArmed
                            ? "Drag to draw a freehand selection."
                              : editRoot.polygonDrawArmed
                                ? "Click to place polygon points. Double-click to finish."
                              : root.objectPickInstructionText()
                color: "#f2f4f6"
                font.family: "Avenir Next"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
        }
    }
}
