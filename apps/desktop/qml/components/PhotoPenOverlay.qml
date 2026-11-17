pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool active: false
    property bool panActive: false
    required property int canvasWidth
    required property int canvasHeight
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    // Optional transform hooks take and return Qt.point values. Return null when
    // the current view/path coordinate cannot be mapped.
    property var mapViewToPath: null
    property var mapPathToView: null
    property var pathDocument: ({schema: "photo-vector-path", version: 1,
        coordinateSpace: "normalized", subpaths: []})
    property var editDocument: ({})
    property var transactionStart: null
    property bool transactionOpen: false
    // Set this for integrations that must acknowledge their controller mutation.
    // A rejected commit leaves the path and transaction snapshot available to retry.
    property bool requireCommitAcknowledgement: false
    property bool commitPending: false
    property string commitError: ""
    property bool canCloseCurrentPath: false
    property bool hasClosedPath: false
    property bool hasOnlyClosedPaths: false
    property bool requireClosedPathToFinish: false
    property bool requireAllPathsClosedToFinish: false
    readonly property bool canFinishPath: transactionOpen && !commitPending &&
                                          (!requireClosedPathToFinish || hasClosedPath) &&
                                          (!requireAllPathsClosedToFinish || hasOnlyClosedPaths)
    readonly property bool canCancelPath: transactionOpen
    property string selectedSubpathId: ""
    property string selectedAnchorId: ""
    readonly property bool hasSelectedAnchor: !!anchorById(
        subpathById(currentDocument(), selectedSubpathId), selectedAnchorId)
    property string dragMode: ""
    property string dragSide: ""
    property bool dragBreakHandles: false
    property point dragStartDocument: Qt.point(0, 0)
    property string dragSubpathId: ""
    property string dragAnchorId: ""
    property int nextId: 1

    signal previewChanged(var pathDocument)
    signal commitRequested(var pathDocument)
    signal cancelRequested(var pathDocument)
    signal mappingFailed(string mapperName)
    signal commitFailed(string reason)
    signal finishedWithoutChanges()

    property string lastMapperFailure: ""

    visible: active
    enabled: active && !panActive && canvasWidth > 0 && canvasHeight > 0 &&
             imageWidth > 0 && imageHeight > 0
    focus: active
    onActiveChanged: {
        if (active)
            forceActiveFocus()
        else if (transactionOpen)
            cancelTransaction()
    }
    onPathDocumentChanged: {
        if (!transactionOpen)
            editDocument = cloneDocument(pathDocument)
        refreshPathCapabilities()
        overlay.requestPaint()
    }
    onEditDocumentChanged: {
        refreshPathCapabilities()
        overlay.requestPaint()
    }
    onImageXChanged: overlay.requestPaint()
    onImageYChanged: overlay.requestPaint()
    onImageWidthChanged: overlay.requestPaint()
    onImageHeightChanged: overlay.requestPaint()
    onSelectedAnchorIdChanged: overlay.requestPaint()
    onSelectedSubpathIdChanged: overlay.requestPaint()

    function cloneDocument(value) {
        const fallback = {schema: "photo-vector-path", version: 1,
                          coordinateSpace: "normalized", subpaths: []}
        try {
            const result = JSON.parse(JSON.stringify(value || fallback))
            if (!Array.isArray(result.subpaths)) result.subpaths = []
            return result
        } catch (error) {
            return fallback
        }
    }

    function currentDocument() {
        if (!editDocument || editDocument.schema !== "photo-vector-path")
            editDocument = cloneDocument(pathDocument)
        return editDocument
    }

    function dataBounds(document) {
        if (String(document.coordinateSpace || "normalized") === "document") {
            const size = Array.isArray(document.documentSize) ? document.documentSize : [canvasWidth, canvasHeight]
            return {width: Math.max(1, Number(size[0] || canvasWidth)),
                    height: Math.max(1, Number(size[1] || canvasHeight))}
        }
        return {width: 1, height: 1}
    }

    function isFinitePoint(value) {
        return !!value && Number.isFinite(Number(value.x)) && Number.isFinite(Number(value.y))
    }

    function reportMappingFailure(mapperName) {
        if (lastMapperFailure === mapperName)
            return
        lastMapperFailure = mapperName
        mappingFailed(mapperName)
    }

    function mappedViewPoint(x, y) {
        if (!Number.isFinite(Number(x)) || !Number.isFinite(Number(y))) {
            reportMappingFailure("mapViewToPath")
            return null
        }
        const input = Qt.point(x, y)
        if (mapViewToPath !== null && mapViewToPath !== undefined &&
                typeof mapViewToPath !== "function") {
            reportMappingFailure("mapViewToPath")
            return null
        }
        if (typeof mapViewToPath === "function") {
            let result = null
            const mapper = mapViewToPath
            try { result = mapper(input) } catch (error) { result = null }
            if (!isFinitePoint(result)) {
                reportMappingFailure("mapViewToPath")
                return null
            }
            lastMapperFailure = ""
            return Qt.point(Number(result.x), Number(result.y))
        }
        if (!(imageWidth > 0 && imageHeight > 0)) {
            reportMappingFailure("mapViewToPath")
            return null
        }
        const bounds = dataBounds(currentDocument())
        const nx = Math.max(0, Math.min(1, (x - imageX) / imageWidth))
        const ny = Math.max(0, Math.min(1, (y - imageY) / imageHeight))
        const result = Qt.point(nx * bounds.width, ny * bounds.height)
        if (!isFinitePoint(result)) {
            reportMappingFailure("mapViewToPath")
            return null
        }
        lastMapperFailure = ""
        return result
    }

    function documentPoint(x, y) {
        return mappedViewPoint(x, y)
    }

    function pathPoint(value) {
        if (Array.isArray(value) && value.length >= 2 &&
                Number.isFinite(Number(value[0])) && Number.isFinite(Number(value[1])))
            return Qt.point(Number(value[0]), Number(value[1]))
        if (isFinitePoint(value))
            return Qt.point(Number(value.x), Number(value.y))
        return null
    }

    function screenPoint(value, document) {
        const source = pathPoint(value)
        if (!source) {
            reportMappingFailure("mapPathToView")
            return null
        }
        if (mapPathToView !== null && mapPathToView !== undefined &&
                typeof mapPathToView !== "function") {
            reportMappingFailure("mapPathToView")
            return null
        }
        if (typeof mapPathToView === "function") {
            let result = null
            const mapper = mapPathToView
            try { result = mapper(source) } catch (error) { result = null }
            if (!isFinitePoint(result)) {
                reportMappingFailure("mapPathToView")
                return null
            }
            lastMapperFailure = ""
            return Qt.point(Number(result.x), Number(result.y))
        }
        if (!(imageWidth > 0 && imageHeight > 0)) {
            reportMappingFailure("mapPathToView")
            return null
        }
        const bounds = dataBounds(document)
        const result = Qt.point(imageX + source.x * imageWidth / bounds.width,
                                imageY + source.y * imageHeight / bounds.height)
        if (!isFinitePoint(result)) {
            reportMappingFailure("mapPathToView")
            return null
        }
        lastMapperFailure = ""
        return result
    }

    function pointArray(point) {
        return [Number(point.x), Number(point.y)]
    }

    function samePoint(point) {
        return Array.isArray(point) && Number(point[0]) === 0 && Number(point[1]) === 0
    }

    function beginTransaction() {
        if (transactionOpen)
            return
        transactionStart = cloneDocument(currentDocument())
        transactionOpen = true
    }

    function publishPreview() {
        commitError = ""
        refreshPathCapabilities()
        overlay.requestPaint()
        previewChanged(cloneDocument(currentDocument()))
    }

    function commitTransaction() {
        if (!canFinishPath || commitPending)
            return
        const snapshot = cloneDocument(currentDocument())
        dragMode = ""
        if (transactionStart && JSON.stringify(snapshot) === JSON.stringify(transactionStart)) {
            transactionOpen = false
            transactionStart = null
            commitError = ""
            finishedWithoutChanges()
            overlay.requestPaint()
            return
        }
        commitError = ""
        commitPending = true
        commitRequested(snapshot)
        // Preserve standalone behavior unless the integration opts in to ack.
        if (!requireCommitAcknowledgement && commitPending)
            acceptCommit()
        overlay.requestPaint()
    }

    function acceptCommit() {
        if (!commitPending)
            return false
        commitPending = false
        transactionOpen = false
        transactionStart = null
        commitError = ""
        overlay.requestPaint()
        return true
    }

    function rejectCommit(reason) {
        if (!commitPending)
            return false
        commitPending = false
        transactionOpen = true
        commitError = String(reason || qsTr("The path could not be applied. Try again or cancel."))
        commitFailed(commitError)
        overlay.requestPaint()
        return true
    }

    function finishPath() {
        commitTransaction()
    }

    function cancelPath() {
        return cancelTransaction()
    }

    function openSubpathToClose() {
        const document = currentDocument()
        if (!document.subpaths || document.subpaths.length === 0)
            return null
        for (let index = document.subpaths.length - 1; index >= 0; --index) {
            const subpath = document.subpaths[index]
            if (!subpath.closed && subpath.anchors && subpath.anchors.length >= 3)
                return subpath
        }
        return null
    }

    function canCloseOpenPath() {
        return openSubpathToClose() !== null
    }

    function refreshPathCapabilities() {
        canCloseCurrentPath = canCloseOpenPath()
        const document = currentDocument()
        hasClosedPath = !!document && Array.isArray(document.subpaths) &&
                document.subpaths.some(function(subpath) {
                    return !!subpath && subpath.closed === true &&
                           Array.isArray(subpath.anchors) && subpath.anchors.length >= 3
                })
        hasOnlyClosedPaths = !!document && Array.isArray(document.subpaths) &&
                document.subpaths.length > 0 && document.subpaths.every(function(subpath) {
                    return !!subpath && subpath.closed === true &&
                           Array.isArray(subpath.anchors) && subpath.anchors.length >= 3
                })
    }

    function closeOpenPath() {
        const subpath = openSubpathToClose()
        if (!subpath)
            return false
        beginTransaction()
        subpath.closed = true
        selectedSubpathId = String(subpath.id)
        selectedAnchorId = String(subpath.anchors[0].id)
        publishPreview()
        return true
    }

    function cancelTransaction() {
        if (!transactionOpen)
            return false
        const restored = cloneDocument(transactionStart)
        editDocument = restored
        refreshPathCapabilities()
        transactionOpen = false
        commitPending = false
        transactionStart = null
        commitError = ""
        dragMode = ""
        selectedAnchorId = ""
        cancelRequested(cloneDocument(restored))
        overlay.requestPaint()
        return true
    }

    function makeId(prefix) {
        return prefix + "-" + Date.now().toString(36) + "-" + (nextId++).toString(36)
    }

    function subpathById(document, id) {
        for (const subpath of document.subpaths)
            if (String(subpath.id) === id)
                return subpath
        return null
    }

    function anchorById(subpath, id) {
        if (!subpath)
            return null
        for (const anchor of subpath.anchors)
            if (String(anchor.id) === id)
                return anchor
        return null
    }

    function makeAnchor(point) {
        return {id: makeId("anchor"), point: pointArray(point), in: [0, 0], out: [0, 0],
                handlesLinked: false, kind: "corner"}
    }

    function coordinateScale(document) {
        return String(document.coordinateSpace || "normalized") === "normalized" ? 100 : 1
    }

    function selectedAnchorPosition(axis) {
        const document = currentDocument()
        const anchor = anchorById(subpathById(document, selectedSubpathId), selectedAnchorId)
        if (!anchor || !Array.isArray(anchor.point))
            return NaN
        return Number(anchor.point[axis === "y" ? 1 : 0]) * coordinateScale(document)
    }

    function setSelectedAnchorPosition(axis, value) {
        const document = currentDocument()
        const anchor = anchorById(subpathById(document, selectedSubpathId), selectedAnchorId)
        const numericValue = Number(value)
        if (!anchor || (axis !== "x" && axis !== "y") || !Number.isFinite(numericValue))
            return false
        const index = axis === "y" ? 1 : 0
        const coordinate = numericValue / coordinateScale(document)
        if (Number(anchor.point[index]) === coordinate)
            return true
        beginTransaction()
        const moved = anchor.point.slice()
        moved[index] = coordinate
        anchor.point = moved
        publishPreview()
        return true
    }

    function selectedHandleState() {
        const anchor = anchorById(subpathById(currentDocument(), selectedSubpathId), selectedAnchorId)
        if (!anchor)
            return "none"
        return anchor.handlesLinked === true ? "linked" : "broken"
    }

    function linkAnchorHandles(anchor) {
        const incoming = pathPoint(anchor.in) || Qt.point(0, 0)
        const outgoing = pathPoint(anchor.out) || Qt.point(0, 0)
        let direction = Math.hypot(outgoing.x, outgoing.y) > 0
                ? outgoing : Qt.point(-incoming.x, -incoming.y)
        const length = Math.hypot(direction.x, direction.y)
        if (length > 0) {
            direction = Qt.point(direction.x / length, direction.y / length)
            const incomingLength = Math.hypot(incoming.x, incoming.y)
            const outgoingLength = Math.hypot(outgoing.x, outgoing.y)
            anchor.in = [-direction.x * incomingLength, -direction.y * incomingLength]
            anchor.out = [direction.x * outgoingLength, direction.y * outgoingLength]
        }
        anchor.handlesLinked = true
    }

    function setSelectedAnchorKind(kind) {
        if (kind !== "corner" && kind !== "smooth")
            return false
        const anchor = anchorById(subpathById(currentDocument(), selectedSubpathId), selectedAnchorId)
        if (!anchor)
            return false
        const linked = kind === "smooth"
        if (anchor.kind === kind && anchor.handlesLinked === linked)
            return true
        beginTransaction()
        if (linked)
            linkAnchorHandles(anchor)
        else
            anchor.handlesLinked = false
        anchor.kind = kind
        publishPreview()
        return true
    }

    function setSelectedHandlesLinked(linked) {
        const anchor = anchorById(subpathById(currentDocument(), selectedSubpathId), selectedAnchorId)
        if (!anchor)
            return false
        linked = linked === true
        if (anchor.handlesLinked === linked)
            return true
        beginTransaction()
        if (linked) {
            linkAnchorHandles(anchor)
            anchor.kind = "smooth"
        } else {
            anchor.handlesLinked = false
            anchor.kind = "corner"
        }
        publishPreview()
        return true
    }

    function deleteSelectedAnchor() {
        const document = currentDocument()
        const subpath = subpathById(document, selectedSubpathId)
        const anchorIndex = subpath ? subpath.anchors.findIndex(function(anchor) {
            return String(anchor.id) === selectedAnchorId
        }) : -1
        if (!subpath || anchorIndex < 0)
            return false
        beginTransaction()
        subpath.anchors.splice(anchorIndex, 1)
        if (subpath.anchors.length === 0) {
            const subpathIndex = document.subpaths.indexOf(subpath)
            if (subpathIndex >= 0)
                document.subpaths.splice(subpathIndex, 1)
            selectedSubpathId = ""
            selectedAnchorId = ""
        } else {
            if (subpath.anchors.length < 3)
                subpath.closed = false
            const nextIndex = Math.min(anchorIndex, subpath.anchors.length - 1)
            selectedSubpathId = String(subpath.id)
            selectedAnchorId = String(subpath.anchors[nextIndex].id)
        }
        publishPreview()
        return true
    }

    function nearestAnchor(x, y) {
        const document = currentDocument()
        let nearest = null
        let nearestDistance = 14 * 14
        for (const subpath of document.subpaths) {
            for (const anchor of subpath.anchors) {
                const point = screenPoint(anchor.point, document)
                if (!point)
                    continue
                const dx = point.x - x
                const dy = point.y - y
                const distance = dx * dx + dy * dy
                if (distance <= nearestDistance) {
                    nearestDistance = distance
                    nearest = {subpath: subpath, anchor: anchor, distance: distance}
                }
            }
        }
        return nearest
    }

    function cubicPoint(p0, p1, p2, p3, t) {
        const u = 1 - t
        const uu = u * u
        const tt = t * t
        return Qt.point(uu * u * p0.x + 3 * uu * t * p1.x +
                        3 * u * tt * p2.x + tt * t * p3.x,
                        uu * u * p0.y + 3 * uu * t * p1.y +
                        3 * u * tt * p2.y + tt * t * p3.y)
    }

    function segmentControls(subpath, startIndex) {
        const endIndex = (startIndex + 1) % subpath.anchors.length
        const start = subpath.anchors[startIndex]
        const end = subpath.anchors[endIndex]
        const p0 = pathPoint(start.point)
        const p3 = pathPoint(end.point)
        const out = pathPoint(start.out) || Qt.point(0, 0)
        const incoming = pathPoint(end.in) || Qt.point(0, 0)
        if (!p0 || !p3)
            return null
        return {p0: p0, p1: Qt.point(p0.x + out.x, p0.y + out.y),
                p2: Qt.point(p3.x + incoming.x, p3.y + incoming.y), p3: p3,
                endIndex: endIndex}
    }

    function distanceToSegmentSquared(point, a, b) {
        const dx = b.x - a.x
        const dy = b.y - a.y
        const lengthSquared = dx * dx + dy * dy
        const t = lengthSquared > 0
                ? Math.max(0, Math.min(1, ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared))
                : 0
        const px = a.x + t * dx
        const py = a.y + t * dy
        const ox = point.x - px
        const oy = point.y - py
        return {distance: ox * ox + oy * oy, fraction: t}
    }

    function nearestSegment(x, y) {
        const document = currentDocument()
        const target = Qt.point(x, y)
        let nearest = null
        let nearestDistance = 11 * 11
        for (const subpath of document.subpaths) {
            if (!subpath.anchors || subpath.anchors.length < 2)
                continue
            const segmentCount = subpath.closed ? subpath.anchors.length : subpath.anchors.length - 1
            for (let startIndex = 0; startIndex < segmentCount; ++startIndex) {
                const curve = segmentControls(subpath, startIndex)
                if (!curve)
                    continue
                let previousT = 0
                let previousPoint = screenPoint(curve.p0, document)
                if (!previousPoint)
                    continue
                for (let sample = 1; sample <= 32; ++sample) {
                    const t = sample / 32
                    const source = cubicPoint(curve.p0, curve.p1, curve.p2, curve.p3, t)
                    const view = screenPoint(source, document)
                    if (!view) {
                        previousT = t
                        previousPoint = null
                        continue
                    }
                    if (previousPoint) {
                        const closest = distanceToSegmentSquared(target, previousPoint, view)
                        if (closest.distance <= nearestDistance) {
                            nearestDistance = closest.distance
                            nearest = {subpath: subpath, startIndex: startIndex,
                                       endIndex: curve.endIndex,
                                       t: previousT + (t - previousT) * closest.fraction,
                                       distance: closest.distance}
                        }
                    }
                    previousT = t
                    previousPoint = view
                }
            }
        }
        return nearest
    }

    function addAnchorOnSegment(segment) {
        if (!segment || !segment.subpath || !Number.isFinite(segment.t))
            return null
        const subpath = segment.subpath
        const curve = segmentControls(subpath, segment.startIndex)
        if (!curve)
            return null
        const t = Math.max(0.001, Math.min(0.999, segment.t))
        const p0 = curve.p0
        const p1 = curve.p1
        const p2 = curve.p2
        const p3 = curve.p3
        const lerp = function(left, right, amount) {
            return Qt.point(left.x + (right.x - left.x) * amount,
                            left.y + (right.y - left.y) * amount)
        }
        const q0 = lerp(p0, p1, t)
        const q1 = lerp(p1, p2, t)
        const q2 = lerp(p2, p3, t)
        const r0 = lerp(q0, q1, t)
        const r1 = lerp(q1, q2, t)
        const split = lerp(r0, r1, t)
        const start = subpath.anchors[segment.startIndex]
        const end = subpath.anchors[segment.endIndex]
        start.out = [q0.x - p0.x, q0.y - p0.y]
        end.in = [q2.x - p3.x, q2.y - p3.y]
        const inserted = {id: makeId("anchor"), point: pointArray(split),
                          in: [r0.x - split.x, r0.y - split.y],
                          out: [r1.x - split.x, r1.y - split.y],
                          handlesLinked: true, kind: "smooth"}
        const insertionIndex = segment.endIndex === 0 ? subpath.anchors.length : segment.endIndex
        subpath.anchors.splice(insertionIndex, 0, inserted)
        selectedSubpathId = String(subpath.id)
        selectedAnchorId = String(inserted.id)
        return inserted
    }

    function selectedHandleAt(x, y) {
        const document = currentDocument()
        const subpath = subpathById(document, selectedSubpathId)
        const anchor = anchorById(subpath, selectedAnchorId)
        if (!anchor)
            return ""
        const origin = screenPoint(anchor.point, document)
        if (!origin)
            return ""
        for (const side of ["in", "out"]) {
            const offset = anchor[side]
            if (samePoint(offset))
                continue
            const handle = screenPoint([Number(anchor.point[0]) + Number(offset[0]),
                                        Number(anchor.point[1]) + Number(offset[1])], document)
            if (!handle)
                continue
            const dx = handle.x - x
            const dy = handle.y - y
            if (dx * dx + dy * dy <= 12 * 12)
                return side
        }
        return ""
    }

    function closeOnFirstAnchor(point) {
        const document = currentDocument()
        if (document.subpaths.length === 0)
            return false
        const subpath = document.subpaths[document.subpaths.length - 1]
        if (subpath.closed || subpath.anchors.length < 3)
            return false
        const first = screenPoint(subpath.anchors[0].point, document)
        const target = screenPoint(pointArray(point), document)
        if (!first || !target)
            return false
        const dx = first.x - target.x
        const dy = first.y - target.y
        if (dx * dx + dy * dy > 14 * 14)
            return false
        beginTransaction()
        subpath.closed = true
        selectedSubpathId = String(subpath.id)
        selectedAnchorId = String(subpath.anchors[0].id)
        return true
    }

    function addAnchor(point) {
        const document = currentDocument()
        let subpath = document.subpaths.length > 0
                ? document.subpaths[document.subpaths.length - 1] : null
        if (!subpath || subpath.closed) {
            subpath = {id: makeId("subpath"), closed: false, anchors: []}
            document.subpaths.push(subpath)
        }
        const newAnchor = makeAnchor(point)
        subpath.anchors.push(newAnchor)
        selectedSubpathId = String(subpath.id)
        selectedAnchorId = String(newAnchor.id)
        dragSubpathId = selectedSubpathId
        dragAnchorId = selectedAnchorId
        dragStartDocument = point
        dragMode = "newHandle"
    }

    function moveSelectedAnchor(point) {
        const anchor = anchorById(subpathById(currentDocument(), dragSubpathId), dragAnchorId)
        if (anchor)
            anchor.point = pointArray(point)
    }

    function moveSelectedHandle(point) {
        const anchor = anchorById(subpathById(currentDocument(), dragSubpathId), dragAnchorId)
        if (!anchor)
            return
        const delta = Qt.point(point.x - Number(anchor.point[0]), point.y - Number(anchor.point[1]))
        anchor[dragSide] = pointArray(delta)
        if (dragBreakHandles)
            anchor.handlesLinked = false
        if (anchor.handlesLinked) {
            const other = dragSide === "in" ? "out" : "in"
            anchor[other] = [-delta.x, -delta.y]
        }
    }

    function createHandlesFromDrag(point) {
        const anchor = anchorById(subpathById(currentDocument(), dragSubpathId), dragAnchorId)
        if (!anchor)
            return
        const dx = point.x - dragStartDocument.x
        const dy = point.y - dragStartDocument.y
        const bounds = dataBounds(currentDocument())
        const minimum = Math.max(bounds.width, bounds.height) * 0.002
        if (Math.hypot(dx, dy) < minimum)
            return
        if (dragBreakHandles) {
            anchor.in = [0, 0]
            anchor.out = [dx, dy]
            anchor.handlesLinked = false
            anchor.kind = "corner"
        } else {
            anchor.in = [-dx, -dy]
            anchor.out = [dx, dy]
            anchor.handlesLinked = true
            anchor.kind = "smooth"
        }
    }

    function appendSubpathToCanvas(ctx, subpath, document) {
        if (!subpath.anchors || subpath.anchors.length === 0)
            return true
        const first = subpath.anchors[0]
        const firstPoint = screenPoint(first.point, document)
        if (!firstPoint)
            return false
        ctx.moveTo(firstPoint.x, firstPoint.y)
        const segmentCount = subpath.closed ? subpath.anchors.length : subpath.anchors.length - 1
        for (let i = 0; i < segmentCount; ++i) {
            const start = subpath.anchors[i]
            const end = subpath.anchors[(i + 1) % subpath.anchors.length]
            const p3 = screenPoint(end.point, document)
            if (!p3)
                return false
            const out = start.out || [0, 0]
            const incoming = end.in || [0, 0]
            if (samePoint(out) && samePoint(incoming)) {
                ctx.lineTo(p3.x, p3.y)
            } else {
                const p1 = screenPoint([Number(start.point[0]) + Number(out[0]),
                                        Number(start.point[1]) + Number(out[1])], document)
                const p2 = screenPoint([Number(end.point[0]) + Number(incoming[0]),
                                        Number(end.point[1]) + Number(incoming[1])], document)
                if (!p1 || !p2)
                    return false
                ctx.bezierCurveTo(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y)
            }
        }
        if (subpath.closed)
            ctx.closePath()
        return true
    }

    function drawPath(ctx, document) {
        ctx.clearRect(0, 0, width, height)
        if (!document || !Array.isArray(document.subpaths))
            return
        ctx.save()
        ctx.beginPath()
        for (const subpath of document.subpaths) {
            if (!subpath.closed || !subpath.anchors || subpath.anchors.length < 3)
                continue
            if (!appendSubpathToCanvas(ctx, subpath, document)) {
                ctx.restore()
                return
            }
        }
        ctx.fillStyle = "rgba(82, 138, 255, 0.10)"
        try { ctx.fill("evenodd") } catch (error) { ctx.fill() }

        // Open contours remain stroke-only. The native vector layer rasterizer
        // fills explicitly closed subpaths, so the edit preview must match it.
        ctx.beginPath()
        for (const subpath of document.subpaths) {
            if (!appendSubpathToCanvas(ctx, subpath, document)) {
                ctx.restore()
                return
            }
        }
        ctx.lineWidth = 2
        ctx.lineJoin = "round"
        ctx.lineCap = "round"
        ctx.strokeStyle = "#82aaff"
        ctx.stroke()
        const selected = anchorById(subpathById(document, selectedSubpathId), selectedAnchorId)
        if (selected) {
            const center = screenPoint(selected.point, document)
            ctx.lineWidth = 1
            ctx.strokeStyle = "#dbe7ff"
            ctx.fillStyle = "#dbe7ff"
            for (const side of ["in", "out"]) {
                const offset = selected[side] || [0, 0]
                if (samePoint(offset))
                    continue
                const handle = screenPoint([Number(selected.point[0]) + Number(offset[0]),
                                            Number(selected.point[1]) + Number(offset[1])], document)
                if (!handle) {
                    ctx.restore()
                    return
                }
                ctx.beginPath()
                ctx.moveTo(center.x, center.y)
                ctx.lineTo(handle.x, handle.y)
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(handle.x, handle.y, 4, 0, Math.PI * 2)
                ctx.fill()
            }
        }
        for (const subpath of document.subpaths) {
            for (const anchor of subpath.anchors) {
                const point = screenPoint(anchor.point, document)
                if (!point) {
                    ctx.restore()
                    return
                }
                const selectedAnchor = String(anchor.id) === selectedAnchorId &&
                                       String(subpath.id) === selectedSubpathId
                if (selectedAnchor) {
                    ctx.beginPath()
                    ctx.arc(point.x, point.y, 8, 0, Math.PI * 2)
                    ctx.lineWidth = 2
                    ctx.strokeStyle = "#9bc3ff"
                    ctx.stroke()
                }
                ctx.beginPath()
                if (anchor.kind === "corner")
                    ctx.rect(point.x - 4, point.y - 4, 8, 8)
                else
                    ctx.arc(point.x, point.y, 4, 0, Math.PI * 2)
                ctx.fillStyle = selectedAnchor ? "#82aaff" : "#f5f7ff"
                ctx.fill()
                ctx.lineWidth = 1.5
                ctx.strokeStyle = "#132033"
                ctx.stroke()
            }
        }
        ctx.restore()
    }

    Keys.onPressed: function(event) {
        const focusTraversalKey = event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab
        if (commitPending && event.key !== Qt.Key_Escape && !focusTraversalKey) {
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape) {
            cancelTransaction()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            commitTransaction()
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            deleteSelectedAnchor()
            event.accepted = true
        }
    }

    Canvas {
        id: overlay
        anchors.fill: parent
        onPaint: root.drawPath(getContext("2d"), root.currentDocument())
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled && !root.commitPending
        acceptedButtons: Qt.LeftButton
        cursorShape: root.dragMode === "anchor" || root.dragMode === "handle" ? Qt.ClosedHandCursor : Qt.CrossCursor
        preventStealing: true
        onPressed: function(mouse) {
            if (mouse.x < root.imageX || mouse.y < root.imageY ||
                    mouse.x > root.imageX + root.imageWidth ||
                    mouse.y > root.imageY + root.imageHeight)
                return
            root.forceActiveFocus()
            const point = root.documentPoint(mouse.x, mouse.y)
            if (!point)
                return
            const handleSide = root.selectedHandleAt(mouse.x, mouse.y)
            if (handleSide !== "") {
                root.beginTransaction()
                root.dragMode = "handle"
                root.dragSide = handleSide
                root.dragSubpathId = root.selectedSubpathId
                root.dragAnchorId = root.selectedAnchorId
                root.dragStartDocument = point
                root.dragBreakHandles = (mouse.modifiers & Qt.AltModifier) !== 0
                if (root.dragBreakHandles) {
                    const selected = root.anchorById(
                                root.subpathById(root.currentDocument(), root.dragSubpathId),
                                root.dragAnchorId)
                    if (selected)
                        selected.handlesLinked = false
                }
                return
            }
            const nearest = root.nearestAnchor(mouse.x, mouse.y)
            if (nearest) {
                const subpathId = String(nearest.subpath.id)
                const anchorId = String(nearest.anchor.id)
                if (root.closeOnFirstAnchor(point)) {
                    root.publishPreview()
                    return
                }
                root.selectedSubpathId = subpathId
                root.selectedAnchorId = anchorId
                root.beginTransaction()
                root.dragMode = "anchor"
                root.dragSubpathId = subpathId
                root.dragAnchorId = anchorId
                root.dragStartDocument = point
                root.dragBreakHandles = false
                return
            }
            const segment = root.nearestSegment(mouse.x, mouse.y)
            if (segment) {
                root.beginTransaction()
                const inserted = root.addAnchorOnSegment(segment)
                if (inserted) {
                    root.dragMode = "anchor"
                    root.dragSubpathId = String(segment.subpath.id)
                    root.dragAnchorId = String(inserted.id)
                    root.dragStartDocument = point
                    root.dragBreakHandles = false
                    root.publishPreview()
                    return
                }
            }
            root.beginTransaction()
            root.dragBreakHandles = (mouse.modifiers & Qt.AltModifier) !== 0
            root.addAnchor(point)
            root.publishPreview()
        }
        onPositionChanged: function(mouse) {
            if (!(mouse.buttons & Qt.LeftButton) || root.dragMode === "")
                return
            const point = root.documentPoint(mouse.x, mouse.y)
            if (!point)
                return
            if ((mouse.modifiers & Qt.AltModifier) !== 0)
                root.dragBreakHandles = true
            if (root.dragMode === "anchor")
                root.moveSelectedAnchor(point)
            else if (root.dragMode === "handle")
                root.moveSelectedHandle(point)
            else if (root.dragMode === "newHandle")
                root.createHandlesFromDrag(point)
            root.publishPreview()
        }
        onReleased: function(mouse) {
            if (root.dragMode === "newHandle") {
                const point = root.documentPoint(mouse.x, mouse.y)
                if (point)
                    root.createHandlesFromDrag(point)
            }
            if (root.dragMode !== "")
                root.publishPreview()
            root.dragMode = ""
            root.dragBreakHandles = false
        }
        onCanceled: {
            root.dragMode = ""
            root.dragBreakHandles = false
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 14
        width: Math.min(560, Math.max(1, parent.width - 28))
        height: hint.implicitHeight + 16
        radius: 10
        color: "#e51a2432"
        border.color: "#536987"
        enabled: false
        Text {
            id: hint
            anchors.centerIn: parent
            width: Math.max(1, parent.width - 24)
            text: root.transactionOpen
                  ? qsTr("Click to add • drag anchors or handles • Alt breaks handles • Enter apply • Esc cancel")
                  : qsTr("Click to place an anchor • drag for curves • Alt breaks handles • Enter apply • Esc cancel")
            color: "#e9eff8"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    Component.onCompleted: {
        editDocument = cloneDocument(pathDocument)
        refreshPathCapabilities()
        if (active)
            forceActiveFocus()
    }
}
