pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    objectName: "photoLayerStyleOverlay"

    Theme { id: theme }

    property bool active: false
    property bool editable: true
    property bool locked: false
    property bool panActive: false
    property string documentToken: ""
    property int layerIndex: -1
    property string targetLayerId: ""
    property string styleId: ""
    property string styleType: ""
    property var styleParameters: ({})
    property string styleRevision: ""
    property real globalLightAngleDegrees: 45
    property real styleWidth: 0
    property real styleHeight: 0
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0

    // Both functions use local native-raster pixels. The host maps through
    // CanvasCoordinateMapper and the target layer transform in both directions.
    property var mapViewToStylePoint: null
    property var mapStylePointToView: null
    property real styleScaleX: 1
    property real styleScaleY: 1

    // The host owns the history transaction and verifies the captured target on
    // every callback. A successful preview means accepted or unchanged.
    property var beginGesture: null
    property var previewGesture: null
    property var finishGesture: null

    readonly property string pointerCoordinateMappingContract:
        "photo-layer-style-pointer-mapper:v1|CanvasCoordinateMapper|target-layer-transform|local-native-raster-pixels|crop-rotation-aware|shadow-scale-x-y:true|roundtrip:true"
    readonly property bool supportsShadow: styleType === "drop_shadow" || styleType === "inner_shadow"
    readonly property bool supportsGradient: styleType === "gradient_overlay" ||
        (styleType === "stroke" && styleParameters && styleParameters.fill &&
         styleParameters.fill.kind === "gradient" && styleParameters.fill.gradient)
    readonly property bool hasStyleGeometry: styleWidth > 0 && styleHeight > 0 &&
        styleScaleX > 0 && styleScaleY > 0
    readonly property bool overlayAvailable: active && layerIndex >= 0 && targetLayerId.length > 0 &&
        styleId.length > 0 && hasStyleGeometry && (supportsShadow || supportsGradient) &&
        imageWidth > 0 && imageHeight > 0 && typeof mapViewToStylePoint === "function" &&
        typeof mapStylePointToView === "function"
    readonly property bool canEdit: overlayAvailable && editable && !locked && !panActive &&
        typeof beginGesture === "function" && typeof previewGesture === "function" &&
        typeof finishGesture === "function"
    readonly property bool interactionActive: drag.interacting
    readonly property string dragKind: drag.kind
    readonly property color guideColor: locked || !editable ? theme.textMuted : theme.primaryAccent
    readonly property var displayParameters: drag.previewParameters !== null
        ? drag.previewParameters : styleParameters

    signal interactionStarted(string kind, string targetLayerId, string styleId)
    signal interactionFinished(bool committed, string kind, string targetLayerId, string styleId)
    signal mappingFailed(string direction)

    visible: overlayAvailable
    enabled: canEdit
    focus: active

    onActiveChanged: {
        if (!active)
            cancelInteraction()
        overlay.requestPaint()
    }
    onEditableChanged: if (!editable) cancelInteraction()
    onLockedChanged: if (locked) cancelInteraction()
    onPanActiveChanged: if (panActive) cancelInteraction()
    onEnabledChanged: if (!enabled) cancelInteraction()
    onDocumentTokenChanged: cancelInteraction()
    onLayerIndexChanged: cancelInteraction()
    onTargetLayerIdChanged: cancelInteraction()
    onStyleIdChanged: cancelInteraction()
    onStyleTypeChanged: {
        cancelInteraction()
        overlay.requestPaint()
    }
    onStyleRevisionChanged: cancelInteraction()
    onStyleParametersChanged: overlay.requestPaint()
    onGlobalLightAngleDegreesChanged: overlay.requestPaint()
    onStyleWidthChanged: overlay.requestPaint()
    onStyleHeightChanged: overlay.requestPaint()
    onImageXChanged: overlay.requestPaint()
    onImageYChanged: overlay.requestPaint()
    onImageWidthChanged: overlay.requestPaint()
    onImageHeightChanged: overlay.requestPaint()
    onMapViewToStylePointChanged: overlay.requestPaint()
    onMapStylePointToViewChanged: overlay.requestPaint()
    onStyleScaleXChanged: overlay.requestPaint()
    onStyleScaleYChanged: overlay.requestPaint()

    function finitePoint(point) {
        return !!point && Number.isFinite(Number(point.x)) && Number.isFinite(Number(point.y))
    }

    function clamp(value, low, high) {
        return Math.max(low, Math.min(high, Number(value)))
    }

    function cloneObject(value) {
        try {
            return JSON.parse(JSON.stringify(value || ({})))
        } catch (error) {
            return ({})
        }
    }

    function layerCenter() {
        return Qt.point((styleWidth - 1) * 0.5, (styleHeight - 1) * 0.5)
    }

    function currentShadowAngle(parameters) {
        if (drag.interacting && Number.isFinite(drag.previewShadowAngle))
            return Number(drag.previewShadowAngle)
        return parameters.useGlobalLight === true
            ? Number(globalLightAngleDegrees)
            : Number(parameters.angleDegrees || 0)
    }

    function angleVector(angleDegrees, distance) {
        const radians = Number(angleDegrees) * Math.PI / 180
        return Qt.point(Math.cos(radians) * Number(distance), Math.sin(radians) * Number(distance))
    }

    function shadowOffset(angleDegrees, distance) {
        const radians = Number(angleDegrees) * Math.PI / 180
        return Qt.point(Math.cos(radians) * Number(distance) * styleScaleX,
                        Math.sin(radians) * Number(distance) * styleScaleY)
    }

    function gradientExtent(angleDegrees) {
        const radians = Number(angleDegrees) * Math.PI / 180
        const halfWidth = Math.max(0.5, (styleWidth - 1) * 0.5)
        const halfHeight = Math.max(0.5, (styleHeight - 1) * 0.5)
        return Math.abs(Math.cos(radians)) * halfWidth + Math.abs(Math.sin(radians)) * halfHeight
    }

    function gradientParameters(parameters) {
        if (styleType === "stroke") {
            const fill = parameters && parameters.fill ? parameters.fill : ({})
            return fill.gradient || ({})
        }
        return parameters.gradient || ({})
    }

    function setGradientParameters(parameters, gradient) {
        if (styleType === "stroke") {
            const fill = cloneObject(parameters.fill)
            fill.gradient = gradient
            parameters.fill = fill
        } else {
            parameters.gradient = gradient
        }
    }

    function mapStylePoint(point) {
        if (!finitePoint(point) || typeof mapStylePointToView !== "function")
            return null
        try {
            const mapped = mapStylePointToView(Number(point.x), Number(point.y))
            if (!mapped || !Number.isFinite(Number(mapped.x)) || !Number.isFinite(Number(mapped.y)))
                return null
            return Qt.point(Number(mapped.x), Number(mapped.y))
        } catch (error) {
            return null
        }
    }

    function mapViewPoint(x, y) {
        if (typeof mapViewToStylePoint !== "function")
            return null
        try {
            const mapped = mapViewToStylePoint(Number(x), Number(y))
            if (!mapped || mapped.valid === false || !Number.isFinite(Number(mapped.x)) ||
                    !Number.isFinite(Number(mapped.y)))
                return null
            return Qt.point(Number(mapped.x), Number(mapped.y))
        } catch (error) {
            return null
        }
    }

    function handlePoints() {
        const parameters = displayParameters || ({})
        const center = layerCenter()
        const points = {center: mapStylePoint(center), handle: null, label: ""}
        if (supportsShadow) {
            const angle = currentShadowAngle(parameters)
            const offset = shadowOffset(angle, Math.max(0, Number(parameters.distance || 0)))
            points.handle = mapStylePoint(Qt.point(center.x + offset.x, center.y + offset.y))
            points.label = qsTr("Offset %1 px · %2°")
                .arg(Number(parameters.distance || 0).toFixed(1))
                .arg(angle.toFixed(1))
        } else if (supportsGradient) {
            const gradient = gradientParameters(parameters)
            const angle = Number(gradient.angleDegrees || 0)
            const extent = gradientExtent(angle) * Math.max(0.01, Number(gradient.scale || 1))
            const offset = angleVector(angle, extent)
            points.handle = mapStylePoint(Qt.point(center.x + offset.x, center.y + offset.y))
            points.label = qsTr("Angle %1° · Scale %2%")
                .arg(angle.toFixed(1))
                .arg((Number(gradient.scale || 1) * 100).toFixed(0))
        }
        return points
    }

    function pointDistanceSquared(a, b) {
        if (!a || !b)
            return Number.POSITIVE_INFINITY
        const dx = a.x - b.x
        const dy = a.y - b.y
        return dx * dx + dy * dy
    }

    function hitKind(x, y) {
        if (!canEdit)
            return ""
        const points = handlePoints()
        const pointer = Qt.point(x, y)
        const radius = theme.scaled(14)
        if (points.handle && pointDistanceSquared(pointer, points.handle) <= radius * radius)
            return supportsShadow ? "shadow" : "gradient"
        return ""
    }

    function capturedTargetMatches() {
        return active && editable && !locked && !panActive && layerIndex === drag.layerIndex &&
            targetLayerId === drag.targetLayerId && styleId === drag.styleId &&
            styleType === drag.styleType && documentToken === drag.documentToken &&
            styleRevision === drag.styleRevision
    }

    function beginInteraction(kind, viewX, viewY) {
        if (!canEdit || interactionActive)
            return false
        const mapped = mapViewPoint(viewX, viewY)
        if (!mapped) {
            mappingFailed("view-to-style")
            return false
        }
        drag.interacting = true
        drag.kind = kind
        drag.layerIndex = layerIndex
        drag.targetLayerId = targetLayerId
        drag.styleId = styleId
        drag.styleType = styleType
        drag.documentToken = documentToken
        drag.styleRevision = styleRevision
        drag.startParameters = cloneObject(styleParameters)
        drag.previewParameters = null
        drag.startPoint = mapped
        drag.previewShadowAngle = Number.NaN
        drag.previewed = false
        const accepted = beginGesture(drag.layerIndex, drag.targetLayerId, drag.styleId, drag.documentToken)
        if (accepted !== true) {
            resetInteraction()
            return false
        }
        interactionStarted(kind, drag.targetLayerId, drag.styleId)
        forceActiveFocus()
        return true
    }

    function normalizedAngle(degrees, modifiers) {
        let angle = Number(degrees)
        if (modifiers & Qt.ShiftModifier)
            angle = Math.round(angle / 15) * 15
        return clamp(angle, -360, 360)
    }

    function updateInteraction(viewX, viewY, modifiers) {
        if (!interactionActive)
            return false
        if (!capturedTargetMatches()) {
            cancelInteraction()
            return false
        }
        const mapped = mapViewPoint(viewX, viewY)
        if (!mapped) {
            mappingFailed("view-to-style")
            cancelInteraction()
            return false
        }

        const center = layerCenter()
        const dx = mapped.x - center.x
        const dy = mapped.y - center.y
        let patch = ({})
        const parameters = cloneObject(drag.startParameters)
        if (drag.kind === "shadow") {
            const authoredDx = dx / styleScaleX
            const authoredDy = dy / styleScaleY
            const distance = clamp(Math.hypot(authoredDx, authoredDy), 0, 4096)
            const rawAngle = distance > 1e-8 ? Math.atan2(authoredDy, authoredDx) * 180 / Math.PI
                                                : currentShadowAngle(parameters)
            const angle = normalizedAngle(rawAngle, modifiers)
            parameters.distance = distance
            parameters.angleDegrees = angle
            drag.previewShadowAngle = angle
            patch = {parameters: parameters}
        } else if (drag.kind === "gradient") {
            const gradient = cloneObject(gradientParameters(parameters))
            const rawAngle = Math.atan2(dy, dx) * 180 / Math.PI
            const angle = normalizedAngle(rawAngle, modifiers)
            const scale = clamp(Math.hypot(dx, dy) / Math.max(0.5, gradientExtent(angle)), 0.01, 100)
            gradient.angleDegrees = angle
            gradient.scale = scale
            setGradientParameters(parameters, gradient)
            patch = {parameters: parameters}
        } else {
            return false
        }

        drag.previewParameters = parameters
        drag.previewed = true
        overlay.requestPaint()
        let accepted = false
        try {
            accepted = previewGesture(drag.layerIndex, drag.targetLayerId, drag.styleId, patch,
                                      drag.documentToken) === true
        } catch (error) {
            accepted = false
        }
        if (!accepted) {
            cancelInteraction()
            return false
        }
        return true
    }

    function finishInteraction(commit) {
        if (!interactionActive)
            return
        const kind = drag.kind
        const targetId = drag.targetLayerId
        const targetStyleId = drag.styleId
        const index = drag.layerIndex
        const token = drag.documentToken
        const mayCommit = commit && capturedTargetMatches()
        drag.interacting = false
        drag.kind = ""
        drag.previewParameters = null
        drag.previewShadowAngle = Number.NaN
        drag.previewed = false
        try {
            finishGesture(index, targetId, targetStyleId, mayCommit, token)
        } catch (error) {
            // The controller owns failure reporting and rollback.
        }
        interactionFinished(mayCommit, kind, targetId, targetStyleId)
        overlay.requestPaint()
    }

    function cancelInteraction() {
        if (interactionActive)
            finishInteraction(false)
    }

    function resetInteraction() {
        drag.interacting = false
        drag.kind = ""
        drag.previewParameters = null
        drag.previewShadowAngle = Number.NaN
        drag.previewed = false
        overlay.requestPaint()
    }

    function shiftHandleLabel(point, text, textWidth, textHeight) {
        if (!point || !text.length)
            return Qt.point(imageX, imageY)
        return Qt.point(clamp(point.x + theme.scaled(12), theme.scaled(4),
                              Math.max(theme.scaled(4), width - textWidth - theme.scaled(4))),
                        clamp(point.y - theme.scaled(26), theme.scaled(4),
                              Math.max(theme.scaled(4), height - textHeight - theme.scaled(4))))
    }

    onWidthChanged: overlay.requestPaint()
    onHeightChanged: overlay.requestPaint()

    QtObject {
        id: drag
        property bool interacting: false
        property string kind: ""
        property int layerIndex: -1
        property string targetLayerId: ""
        property string styleId: ""
        property string styleType: ""
        property string documentToken: ""
        property string styleRevision: ""
        property var startParameters: ({})
        property var previewParameters: null
        property var startPoint: Qt.point(0, 0)
        property real previewShadowAngle: Number.NaN
        property bool previewed: false
    }

    Canvas {
        id: overlay
        objectName: "photoLayerStyleGuides"
        anchors.fill: parent
        enabled: false
        visible: root.overlayAvailable
        renderTarget: Canvas.FramebufferObject
        renderStrategy: Canvas.Threaded
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            if (!root.overlayAvailable)
                return

            const points = root.handlePoints()
            if (!points.center || !points.handle)
                return
            const accent = root.guideColor
            ctx.save()
            ctx.lineWidth = theme.scaled(1.5)
            ctx.strokeStyle = accent
            ctx.globalAlpha = root.interactionActive ? 0.98 : 0.78
            ctx.beginPath()
            ctx.moveTo(points.center.x, points.center.y)
            ctx.lineTo(points.handle.x, points.handle.y)
            ctx.stroke()

            const radius = theme.scaled(root.interactionActive ? 6 : 5)
            ctx.fillStyle = theme.textStrong
            ctx.strokeStyle = accent
            ctx.lineWidth = theme.scaled(2)
            ctx.beginPath()
            ctx.arc(points.center.x, points.center.y, theme.scaled(3), 0, Math.PI * 2)
            ctx.fill()
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(points.handle.x, points.handle.y, radius, 0, Math.PI * 2)
            ctx.fill()
            ctx.stroke()
            ctx.restore()
        }
    }

    Text {
        id: feedback
        objectName: "photoLayerStyleCanvasFeedback"
        readonly property var handle: root.handlePoints().handle
        readonly property string measurement: root.handlePoints().label
        readonly property point labelPoint: root.shiftHandleLabel(
            handle, measurement, implicitWidth, implicitHeight)
        visible: root.overlayAvailable && !!handle && measurement.length > 0
        x: labelPoint.x
        y: labelPoint.y
        text: measurement
        color: theme.textStrong
        font.family: theme.fontNumeric
        font.pixelSize: theme.typeCaption
        style: Text.Outline
        styleColor: "#101010"
        renderType: Text.NativeRendering
    }

    MouseArea {
        id: input
        objectName: "photoLayerStyleCanvasInput"
        anchors.fill: parent
        enabled: root.canEdit
        hoverEnabled: true
        preventStealing: root.interactionActive
        cursorShape: root.interactionActive ? Qt.ClosedHandCursor
                     : root.canEdit && root.hitKind(mouseX, mouseY).length > 0 ? Qt.OpenHandCursor
                     : Qt.ArrowCursor

        onPressed: function(mouse) {
            const kind = root.hitKind(mouse.x, mouse.y)
            if (!kind) {
                mouse.accepted = false
                return
            }
            if (!root.beginInteraction(kind, mouse.x, mouse.y))
                mouse.accepted = false
        }
        onPositionChanged: function(mouse) {
            if (pressed)
                root.updateInteraction(mouse.x, mouse.y, mouse.modifiers)
        }
        onReleased: function(mouse) {
            root.updateInteraction(mouse.x, mouse.y, mouse.modifiers)
            root.finishInteraction(true)
        }
        onCanceled: root.cancelInteraction()
    }

    Keys.onEscapePressed: function(event) {
        if (root.interactionActive) {
            root.cancelInteraction()
            event.accepted = true
        }
    }

    Component.onDestruction: cancelInteraction()
}
