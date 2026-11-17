pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    objectName: "photoTextDimensionOverlay"

    Theme { id: theme }

    property bool active: false
    property bool editable: true
    property bool locked: false
    property bool panActive: false
    property string documentToken: ""
    property int layerIndex: -1
    property string targetLayerId: ""
    property string revision: ""
    property string mode: ""
    property int geometryRevision: 0
    property var dimension: ({})
    property var geometryProvider: null
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0

    // Geometry point positions are projected by the controller and converted
    // by its workspace provider to local overlay pixels. Pointer coordinates
    // are passed back through mapViewToDimensionPatch, where the controller
    // applies the matching inverse camera and placement mapping.
    property var mapViewToDimensionPatch: null
    property var beginGesture: null
    property var previewGesture: null
    property var finishGesture: null

    readonly property var dimensionGeometry: {
        const requestedIndex = layerIndex
        const requestedLayerId = targetLayerId
        const requestedDocumentToken = documentToken
        const requestedRevision = revision
        // The host increments this for controller preview/control signals so
        // a callback can be re-queried even when the function identity stays
        // the same. Frame dependencies refresh view-space handle positions.
        const requestedGeometryRevision = geometryRevision
        const requestedFrame = imageX + imageY + imageWidth + imageHeight
        if (requestedGeometryRevision < 0 || !Number.isFinite(requestedFrame))
            return ({})
        if (typeof geometryProvider === "function") {
            try {
                return geometryProvider(requestedIndex, requestedLayerId,
                                        requestedDocumentToken, requestedRevision) || ({})
            }
            catch (error) { return ({}) }
        }
        if (controller && typeof controller.photoTextDimensionCanvasGeometry === "function") {
            try { return controller.photoTextDimensionCanvasGeometry(requestedIndex) || ({}) }
            catch (error) { return ({}) }
        }
        return ({})
    }
    property var controller: null

    readonly property string resolvedLayerId: targetLayerId.length > 0
        ? targetLayerId : String(dimensionGeometry.layerId || "")
    readonly property string resolvedDocumentToken: documentToken.length > 0
        ? documentToken : String(dimensionGeometry.documentToken || "")
    readonly property string resolvedRevision: revision.length > 0
        ? revision : String(dimensionGeometry.revision || "")
    readonly property bool geometryMatchesTarget: dimensionGeometry.valid === true &&
        String(dimensionGeometry.layerId || resolvedLayerId) === resolvedLayerId &&
        String(dimensionGeometry.documentToken || resolvedDocumentToken) === resolvedDocumentToken &&
        String(dimensionGeometry.revision || resolvedRevision) === resolvedRevision
    readonly property bool hasCanvasImage: imageWidth > 0 && imageHeight > 0
    readonly property bool overlayAvailable: active && layerIndex >= 0 &&
        resolvedLayerId.length > 0 && resolvedDocumentToken.length > 0 &&
        geometryMatchesTarget && (hasCanvasImage || dimensionGeometry.overlayImageWidth > 0)
    readonly property bool canEdit: {
        const settings = dimensionGeometry.dimension || dimension || ({})
        return overlayAvailable && editable && !locked && !panActive &&
            dimensionGeometry.editable !== false &&
            ["rotate", "depth", "corner-pin"].indexOf(mode) >= 0 &&
            (mode === "corner-pin" ? settings.cornerPinEnabled === true : settings.enabled === true) &&
            typeof mapViewToDimensionPatch === "function" &&
            typeof beginGesture === "function" && typeof previewGesture === "function" &&
            typeof finishGesture === "function"
    }
    readonly property bool interactionActive: drag.interacting
    readonly property string interactionKind: drag.kind
    readonly property color guideColor: locked || !editable ? theme.textMuted : theme.primaryAccent
    readonly property var displayGeometry: dimensionGeometry

    signal interactionStarted(string kind, string targetLayerId)
    signal interactionFinished(bool committed, string kind, string targetLayerId)
    signal mappingFailed(string mode)

    visible: overlayAvailable
    enabled: canEdit
    focus: active

    onActiveChanged: {
        if (!active) cancelInteraction()
        guides.requestPaint()
    }
    onEditableChanged: if (!editable) cancelInteraction()
    onLockedChanged: if (locked) cancelInteraction()
    onPanActiveChanged: if (panActive) cancelInteraction()
    onEnabledChanged: if (!enabled) cancelInteraction()
    onDocumentTokenChanged: cancelInteraction()
    onLayerIndexChanged: cancelInteraction()
    onTargetLayerIdChanged: cancelInteraction()
    onRevisionChanged: cancelInteraction()
    onModeChanged: cancelInteraction()
    onGeometryRevisionChanged: guides.requestPaint()
    onDimensionGeometryChanged: guides.requestPaint()
    onDimensionChanged: guides.requestPaint()
    onImageXChanged: guides.requestPaint()
    onImageYChanged: guides.requestPaint()
    onImageWidthChanged: guides.requestPaint()
    onImageHeightChanged: guides.requestPaint()
    onMapViewToDimensionPatchChanged: guides.requestPaint()

    function finitePoint(point) {
        return !!point && Number.isFinite(Number(point.x)) && Number.isFinite(Number(point.y))
    }

    function pointOrNull(point) {
        return finitePoint(point) ? Qt.point(Number(point.x), Number(point.y)) : null
    }

    function outlinePoints() {
        const geometry = displayGeometry || ({})
        const points = Array.isArray(geometry.outline) ? geometry.outline
            : Array.isArray(geometry.projectedCorners) ? geometry.projectedCorners
            : Array.isArray(geometry.quad) ? geometry.quad : []
        return points.map(point => pointOrNull(point)).filter(point => point !== null)
    }

    function cornerPoints() {
        const geometry = displayGeometry || ({})
        const handles = geometry.handles || ({})
        const corners = handles.corners || geometry.corners || geometry.cornerHandles || ({})
        const aliases = {
            topLeft: corners.topLeft || corners.tl,
            topRight: corners.topRight || corners.tr,
            bottomRight: corners.bottomRight || corners.br,
            bottomLeft: corners.bottomLeft || corners.bl
        }
        return aliases
    }

    function handlePoint(kind) {
        const handles = (displayGeometry && displayGeometry.handles) || ({})
        if (kind === "rotate")
            return pointOrNull(handles.rotate || displayGeometry.rotateHandle)
        if (kind === "depth")
            return pointOrNull(handles.depth || displayGeometry.depthHandle)
        if (kind.indexOf("corner-") === 0) {
            const corner = kind.slice("corner-".length)
            return pointOrNull(cornerPoints()[corner])
        }
        return null
    }

    function pointDistanceSquared(a, b) {
        if (!a || !b) return Number.POSITIVE_INFINITY
        const dx = Number(a.x) - Number(b.x)
        const dy = Number(a.y) - Number(b.y)
        return dx * dx + dy * dy
    }

    function hitKind(x, y) {
        if (!canEdit) return ""
        const pointer = Qt.point(Number(x), Number(y))
        const radius = theme.scaled(15)
        const radiusSquared = radius * radius
        if (mode === "corner-pin") {
            const points = cornerPoints()
            const order = ["topLeft", "topRight", "bottomRight", "bottomLeft"]
            for (let index = 0; index < order.length; ++index) {
                const point = pointOrNull(points[order[index]])
                if (point && pointDistanceSquared(pointer, point) <= radiusSquared)
                    return "corner-" + order[index]
            }
            return ""
        }
        const handle = handlePoint(mode)
        return handle && pointDistanceSquared(pointer, handle) <= radiusSquared ? mode : ""
    }

    function capturedTargetMatches() {
        const geometry = dimensionGeometry || ({})
        return active && editable && !locked && !panActive &&
            layerIndex === drag.layerIndex && resolvedLayerId === drag.targetLayerId &&
            resolvedDocumentToken === drag.documentToken && resolvedRevision === drag.revision &&
            mode === drag.startMode && geometryMatchesTarget &&
            String(geometry.layerId || resolvedLayerId) === drag.targetLayerId &&
            String(geometry.documentToken || resolvedDocumentToken) === drag.documentToken &&
            String(geometry.revision || resolvedRevision) === drag.revision
    }

    function cloneMap(value) {
        try { return JSON.parse(JSON.stringify(value || ({}))) }
        catch (error) { return ({}) }
    }

    function beginInteraction(kind, viewX, viewY) {
        if (!canEdit || interactionActive || !handlePoint(kind))
            return false

        const geometrySnapshot = cloneMap(dimensionGeometry)
        geometrySnapshot.pressViewX = Number(viewX)
        geometrySnapshot.pressViewY = Number(viewY)
        drag.interacting = true
        drag.kind = String(kind)
        drag.startMode = mode
        drag.layerIndex = layerIndex
        drag.targetLayerId = resolvedLayerId
        drag.documentToken = resolvedDocumentToken
        drag.revision = resolvedRevision
        drag.geometry = geometrySnapshot
        drag.startDimension = cloneMap(dimension && Object.keys(dimension).length > 0
                                       ? dimension : dimensionGeometry.dimension)
        drag.previewed = false

        let accepted = false
        try {
            accepted = beginGesture(drag.layerIndex, drag.targetLayerId,
                                    drag.documentToken, drag.revision) === true
        } catch (error) {
            accepted = false
        }
        if (!accepted) {
            resetInteraction()
            return false
        }
        interactionStarted(kind, drag.targetLayerId)
        forceActiveFocus()
        return true
    }

    function updateInteraction(viewX, viewY, modifiers) {
        if (!interactionActive)
            return false
        if (!capturedTargetMatches()) {
            cancelInteraction()
            return false
        }

        // The controller owns the projection/inverse mapping. The overlay only
        // identifies the active handle and supplies pointer coordinates plus
        // the gesture's captured geometry and dimension state.
        let patch = null
        try {
            patch = mapViewToDimensionPatch(drag.kind, Number(viewX), Number(viewY),
                                           cloneMap(drag.startDimension),
                                           cloneMap(drag.geometry), Number(modifiers))
        } catch (error) {
            patch = null
        }
        if (!patch || typeof patch !== "object" || Array.isArray(patch)) {
            mappingFailed(drag.kind)
            cancelInteraction()
            return false
        }

        drag.previewed = true
        let accepted = false
        try {
            accepted = previewGesture(drag.layerIndex, drag.targetLayerId,
                                      patch, drag.documentToken, drag.revision) === true
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
        const index = drag.layerIndex
        const token = drag.documentToken
        const targetRevision = drag.revision
        const mayCommit = commit && capturedTargetMatches()
        drag.interacting = false
        drag.kind = ""
        drag.startMode = ""
        drag.geometry = ({})
        drag.startDimension = ({})
        drag.previewed = false
        try {
            finishGesture(index, targetId, mayCommit, token, targetRevision)
        } catch (error) {
            // The controller owns failure reporting and rollback.
        }
        interactionFinished(mayCommit, kind, targetId)
        guides.requestPaint()
    }

    function cancelInteraction() {
        if (interactionActive)
            finishInteraction(false)
    }

    function resetInteraction() {
        drag.interacting = false
        drag.kind = ""
        drag.startMode = ""
        drag.geometry = ({})
        drag.startDimension = ({})
        drag.previewed = false
        guides.requestPaint()
    }

    onWidthChanged: guides.requestPaint()
    onHeightChanged: guides.requestPaint()

    QtObject {
        id: drag
        property bool interacting: false
        property string kind: ""
        property string startMode: ""
        property int layerIndex: -1
        property string targetLayerId: ""
        property string documentToken: ""
        property string revision: ""
        property var geometry: ({})
        property var startDimension: ({})
        property bool previewed: false
    }

    Canvas {
        id: guides
        objectName: "photoTextDimensionGuides"
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

            const points = root.outlinePoints()
            const accent = root.guideColor
            ctx.save()
            ctx.strokeStyle = accent
            ctx.fillStyle = theme.alpha(accent, 0.9)
            ctx.lineWidth = theme.scaled(root.interactionActive ? 2 : 1.5)
            ctx.globalAlpha = root.interactionActive ? 0.98 : 0.76
            if (points.length >= 2) {
                ctx.beginPath()
                ctx.moveTo(points[0].x, points[0].y)
                for (let index = 1; index < points.length; ++index)
                    ctx.lineTo(points[index].x, points[index].y)
                ctx.closePath()
                ctx.stroke()
            }

            const rotate = root.handlePoint("rotate")
            const depth = root.handlePoint("depth")
            if (rotate && root.mode === "rotate") {
                ctx.beginPath()
                ctx.arc(rotate.x, rotate.y, theme.scaled(7), 0, Math.PI * 2)
                ctx.fill()
                ctx.stroke()
            }
            if (depth && root.mode === "depth") {
                const size = theme.scaled(7)
                ctx.beginPath()
                ctx.moveTo(depth.x, depth.y - size)
                ctx.lineTo(depth.x + size, depth.y)
                ctx.lineTo(depth.x, depth.y + size)
                ctx.lineTo(depth.x - size, depth.y)
                ctx.closePath()
                ctx.fill()
                ctx.stroke()
            }

            if (root.mode === "corner-pin") {
                const corners = root.cornerPoints()
                const names = ["topLeft", "topRight", "bottomRight", "bottomLeft"]
                for (let index = 0; index < names.length; ++index) {
                    const point = root.pointOrNull(corners[names[index]])
                    if (!point) continue
                    const radius = theme.scaled(6)
                    ctx.beginPath()
                    ctx.rect(point.x - radius, point.y - radius, radius * 2, radius * 2)
                    ctx.fill()
                    ctx.stroke()
                }
            }
            ctx.restore()
        }
    }

    Text {
        objectName: "photoTextDimensionCanvasHint"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: theme.space8
        visible: root.overlayAvailable && root.mode.length > 0
        text: root.mode === "rotate" ? qsTr("Drag the circle to rotate")
            : root.mode === "depth" ? qsTr("Drag the diamond to adjust depth")
            : root.mode === "corner-pin" ? qsTr("Drag a corner to place the text plane") : ""
        color: theme.textStrong
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        style: Text.Outline
        styleColor: "#101010"
        renderType: Text.NativeRendering
    }

    MouseArea {
        id: input
        objectName: "photoTextDimensionCanvasInput"
        anchors.fill: parent
        enabled: root.canEdit
        hoverEnabled: true
        preventStealing: root.interactionActive
        cursorShape: root.interactionActive ? Qt.ClosedHandCursor
            : root.canEdit && root.hitKind(mouseX, mouseY).length > 0
              ? Qt.OpenHandCursor : Qt.ArrowCursor

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
