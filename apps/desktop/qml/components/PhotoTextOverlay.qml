pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PhotoEditor

Item {
    id: root

    Theme { id: theme }
    required property string documentId
    property string editingDocumentId: ""
    required property var controller
    required property var layerData
    required property int layerIndex
    required property var transformFrame
    property bool active: false
    property bool editing: false
    property bool pathHandlesActive: false
    property string editingLayerId: ""
    property int editingLayerIndex: -1
    property string originalText: ""
    property string editError: ""
    readonly property int textSelectionStart: input.selectionStart
    readonly property int textSelectionEnd: input.selectionEnd
    readonly property bool textSelectionActive: input.selectionStart >= 0 && input.selectionEnd >= 0 &&
        input.selectionStart !== input.selectionEnd
    property var creationBox: null
    property var creationViewport: null
    readonly property var displayedLayer: creationBox
        ? ({kind: "text", pixelWidth: creationBox.width, pixelHeight: creationBox.height, fontSize: creationBox.fontSize || 48}) : layerData
    readonly property var baseFrame: creationBox && creationViewport ? ({
        frameX: creationViewport.imageX + creationBox.x * creationViewport.imageWidth / creationBox.canvasWidth,
        frameY: creationViewport.imageY + creationBox.y * creationViewport.imageHeight / creationBox.canvasHeight,
        frameWidth: creationBox.width * creationViewport.imageWidth / creationBox.canvasWidth,
        frameHeight: creationBox.height * creationViewport.imageHeight / creationBox.canvasHeight,
        layerRotation: 0
    }) : transformFrame
    readonly property var style: root.displayedLayer.textTypography || (creationBox ? ({
        version: 2, mode: creationBox.pointText ? "point" : "paragraph", paragraphSpacing: 0,
        family: creationBox.fontFamily || "Helvetica", face: "", size: creationBox.fontSize || 48, weight: 400,
        italic: false, underline: false, tracking: 0, lineHeight: 1.2,
        baselineOffset: 0, color: creationBox.color || "#ffffffff"
    }) : ({}))
    readonly property var pathPoints: root.creationBox ? [] : (root.style.pathPoints || [])
    readonly property bool pathHandlesVisible: root.active && root.pathHandlesActive && !root.editing &&
        root.layerData && String(root.layerData.kind || "") === "text" && root.pathPoints.length > 1
    readonly property bool styledText: Number(style.version || 0) >= 1 && Number(style.version || 0) <= 8
    readonly property bool straightTextDraftNotice: pathPoints.length > 1 ||
        String(style.material || "none") !== "none"
    readonly property bool pointText: styledText && style.mode === "point"
    readonly property var pointGeometry: pointText ? textDocumentStyle.pointGeometry : ({})
    readonly property bool validPointGeometry: pointText && Number(pointGeometry.width) > 0 && !pointGeometry.error
    readonly property real logicalWidth: validPointGeometry ? Number(pointGeometry.width) : Math.max(16, Number(displayedLayer.pixelWidth || 512))
    readonly property real logicalHeight: validPointGeometry ? Number(pointGeometry.height) : Math.max(16, Number(displayedLayer.pixelHeight || 128))
    readonly property real originX: validPointGeometry ? Number(pointGeometry.originX) : 0
    readonly property real originY: validPointGeometry ? Number(pointGeometry.originY) : 0
    readonly property var displayedFrame: {
        if (!validPointGeometry) return baseFrame
        const oldWidth = Number(displayedLayer.pixelWidth || 512)
        const oldHeight = Number(displayedLayer.pixelHeight || 128)
        const sx = baseFrame.frameWidth / oldWidth
        const sy = baseFrame.frameHeight / oldHeight
        const dx = (Number(displayedLayer.textOriginX || 0) - oldWidth / 2 - originX + logicalWidth / 2) * sx
        const dy = (Number(displayedLayer.textOriginY || 0) - oldHeight / 2 - originY + logicalHeight / 2) * sy
        const angle = Number(baseFrame.layerRotation || 0) * Math.PI / 180
        return {frameX: baseFrame.frameX + (oldWidth - logicalWidth) * sx / 2 + dx * Math.cos(angle) - dy * Math.sin(angle),
            frameY: baseFrame.frameY + (oldHeight - logicalHeight) * sy / 2 + dx * Math.sin(angle) + dy * Math.cos(angle),
            frameWidth: logicalWidth * sx, frameHeight: logicalHeight * sy, layerRotation: baseFrame.layerRotation}
    }
    signal editFinished(bool committed, bool created)
    onPathPointsChanged: baselineCanvas.requestPaint()
    onStyleChanged: baselineCanvas.requestPaint()

    readonly property int pathHandleCount: Math.min(64, root.pathPoints.length)

    function pathHandlePointIndex(delegateIndex) {
        const count = root.pathPoints.length
        if (count <= 64 || root.pathHandleCount <= 1) return delegateIndex
        return Math.round(delegateIndex * (count - 1) / (root.pathHandleCount - 1))
    }

    function updatePathHandle(index, x, y) {
        if (!root.pathHandlesVisible || index < 0 || index >= root.pathPoints.length ||
                root.baseFrame.frameWidth <= 0 || root.baseFrame.frameHeight <= 0) return
        const points = root.pathPoints.slice()
        const point = Object.assign({}, points[index])
        point.x = Math.max(0, Math.min(1, x / root.baseFrame.frameWidth))
        point.y = Math.max(0, Math.min(1, y / root.baseFrame.frameHeight))
        points[index] = point
        root.controller.setPhotoLayerTypography(root.layerIndex, {pathPoints: points})
    }

    function finishPathHandleDrag() {
        if (!pathHandleDragActive) return
        pathHandleDragActive = false
        draggedPathPoint = -1
        root.controller.endCoalescedEdit()
    }

    property bool pathHandleDragActive: false
    property int draggedPathPoint: -1

    function beginCreation(box, viewport) {
        if (editing || pathHandlesActive || !active || box.width < 16 || box.height < 16 ||
                box.canvasWidth <= 0 || box.canvasHeight <= 0) return false
        editingDocumentId = documentId
        creationBox = box
        creationViewport = viewport
        originalText = ""
        editError = ""
        input.text = box.text === undefined ? "" : String(box.text)
        editing = true
        draftViewport.contentY = 0
        input.forceActiveFocus()
        input.selectAll()
        return true
    }

    function beginEditing() {
        if (editing || pathHandlesActive || !active || layerIndex < 0 || !String(layerData.id || "") ||
                layerData.locked || String(layerData.kind) !== "text") return false
        editError = ""
        editingDocumentId = documentId
        editingLayerId = String(layerData.id)
        editingLayerIndex = layerIndex
        originalText = layerData.text === undefined ? "Text" : String(layerData.text)
        input.text = originalText
        editing = true
        draftViewport.contentY = 0
        input.forceActiveFocus()
        input.selectAll()
        return true
    }
    function finish(commit) {
        if (!editing) return
        // Qt exposes QInputMethod through QObject in its QML type metadata.
        // qmllint disable missing-property
        if (commit) Qt.inputMethod.commit()
        // qmllint enable missing-property
        const created = creationBox !== null
        const shouldCommit = commit && editingDocumentId === documentId && (created ||
            (editingLayerId === String(layerData.id) && editingLayerIndex === layerIndex))
        editing = false
        let accepted = true
        if (shouldCommit && created) {
            const box = creationBox
            controller.beginCoalescedEdit()
            accepted = box.pointText
                ? controller.addPointTextLayer(input.text, box.x, box.y, box.canvasWidth, box.canvasHeight)
                : controller.addParagraphTextLayer(input.text, box.x, box.y,
                    box.width, box.height, box.canvasWidth, box.canvasHeight)
            if (accepted && (box.fontFamily !== undefined || box.fontSize !== undefined || box.color !== undefined)) {
                controller.setPhotoLayerTextStyle(0, box.fontFamily || "Helvetica",
                    Number(box.fontSize || 48), box.color || "#ffffff", "regular", "left")
            }
            controller.endCoalescedEdit()
        } else if (shouldCommit && input.text !== originalText) {
            accepted = controller.setPhotoLayerText(editingLayerIndex, input.text)
        }
        if (!accepted) {
            editing = true
            editError = pointGeometry.error ? String(pointGeometry.error)
                : qsTr("Could not save this text. Your draft is retained; retry Done or cancel.")
            input.forceActiveFocus()
            return
        }
        input.focus = false
        creationBox = null
        creationViewport = null
        editFinished(shouldCommit, created)
    }

    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onAssetChanged() { if (root.editing) root.finish(false) }
    }
    onDocumentIdChanged: {
        pathHandlesActive = false
        if (editing) finish(false)
    }
    onActiveChanged: {
        if (!active) {
            pathHandlesActive = false
            finish(false)
        }
    }
    onLayerIndexChanged: {
        pathHandlesActive = false
        if (editing && !creationBox) finish(false)
    }
    onLayerDataChanged: {
        if (pathHandlesActive && (!root.pathPoints.length || root.layerData.locked || root.creationBox))
            pathHandlesActive = false
        if (editing && !creationBox && (String(layerData.id) !== editingLayerId || layerData.locked ||
                String(layerData.text === undefined ? "Text" : layerData.text) !== originalText)) finish(false)
    }
    visible: active && (editing || pathHandlesVisible)
    enabled: visible

    MouseArea {
        objectName: "photoCanvasTextOutsideArea"
        anchors.fill: parent
        enabled: root.editing
        onClicked: root.finish(true)
    }

    Item {
        id: pathHandleFrame
        objectName: "photoTextPathHandleFrame"
        visible: root.pathHandlesVisible
        x: root.baseFrame.frameX
        y: root.baseFrame.frameY
        width: root.baseFrame.frameWidth
        height: root.baseFrame.frameHeight
        rotation: root.baseFrame.layerRotation
        transformOrigin: Item.Center
        z: 5

        Canvas {
            id: baselineCanvas
            objectName: "photoTextPathBaseline"
            property int paintedContourCount: 0
            anchors.fill: parent
            onVisibleChanged: if (visible) requestPaint()
            onPaint: {
                const points = root.pathPoints
                const count = points.length
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                paintedContourCount = 0
                if (!root.pathHandlesVisible || count < 2) return

                function drawContour(contour, closed, color, dash, lineWidth) {
                    if (!contour || contour.length < 2) return false
                    const contourCount = contour.length
                    ctx.beginPath()
                    ctx.moveTo(contour[0].x * width, contour[0].y * height)
                    const segments = closed ? contourCount : contourCount - 1
                    for (let i = 0; i < segments; ++i) {
                        const p1 = contour[i % contourCount]
                        const p2 = contour[(i + 1) % contourCount]
                        const p0 = contour[closed ? (i - 1 + contourCount) % contourCount : Math.max(0, i - 1)]
                        const p3 = contour[closed ? (i + 2) % contourCount : Math.min(contourCount - 1, i + 2)]
                        const c1x = p1.x + (p2.x - p0.x) / 6
                        const c1y = p1.y + (p2.y - p0.y) / 6
                        const c2x = p2.x - (p3.x - p1.x) / 6
                        const c2y = p2.y - (p3.y - p1.y) / 6
                        ctx.bezierCurveTo(c1x * width, c1y * height, c2x * width, c2y * height,
                            p2.x * width, p2.y * height)
                    }
                    if (closed) ctx.closePath()
                    ctx.setLineDash(dash)
                    ctx.lineWidth = lineWidth
                    ctx.strokeStyle = color
                    ctx.stroke()
                    return true
                }

                if (drawContour(points, !!root.style.pathClosed, theme.primaryAccent, [6, 4], 2))
                    paintedContourCount += 1
                const contours = root.style.pathContours || []
                for (let index = 1; index < contours.length; ++index) {
                    if (drawContour(contours[index], true, "#62d6c4", [3, 4], 1.5))
                        paintedContourCount += 1
                }
            }
        }

        Repeater {
            // Keep the delegate count stable while a point moves. Replacing an
            // array-valued model on every controller update would recreate the
            // active MouseArea during a drag and allocate up to 64 new items.
            model: root.pathHandleCount
            delegate: Item {
                required property int index
                readonly property int pointIndex: root.pathHandlePointIndex(index)
                objectName: "photoTextPathHandlePoint_" + pointIndex
                readonly property var point: root.pathPoints[pointIndex] || ({x: 0, y: 0})
                x: point.x * pathHandleFrame.width - width / 2
                y: point.y * pathHandleFrame.height - height / 2
                width: 20
                height: 20
                z: 2

                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    radius: width / 2
                    color: theme.surfaceTop
                    border.width: 2
                    border.color: theme.primaryAccent
                }

                MouseArea {
                    objectName: "photoTextPathHandle_" + parent.pointIndex
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    preventStealing: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    onPressed: {
                        root.draggedPathPoint = parent.pointIndex
                        root.pathHandleDragActive = true
                        root.controller.beginCoalescedEdit()
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const local = mapToItem(pathHandleFrame, mouse.x, mouse.y)
                        root.updatePathHandle(root.draggedPathPoint, local.x, local.y)
                    }
                    onReleased: root.finishPathHandleDrag()
                    onCanceled: root.finishPathHandleDrag()
                }
            }
        }
    }

    Item {
        id: frame
        visible: root.editing
        x: root.displayedFrame.frameX
        y: root.displayedFrame.frameY
        width: root.displayedFrame.frameWidth
        height: root.displayedFrame.frameHeight
        rotation: root.displayedFrame.layerRotation
        transformOrigin: Item.Center
        Rectangle {
            anchors.fill: parent
            color: "#ee202428"
            border.color: theme.primaryAccent
            border.width: 1
        }
        Flickable {
            id: draftViewport
            objectName: "photoCanvasTextViewport"
            width: root.logicalWidth
            height: root.logicalHeight
            transform: Scale {
                xScale: frame.width / draftViewport.width
                yScale: frame.height / draftViewport.height
            }
            clip: true
            contentWidth: width
            contentHeight: input.height
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            ScrollBar.vertical: ScrollBar { }
            TextEdit {
                id: input
                objectName: "photoCanvasTextInput"
                x: root.originX
                width: Math.max(1, draftViewport.width - root.originX)
                y: root.originY + (root.styledText ? -Number(root.style.baselineOffset || 0) - textDocumentStyle.topInset : 0)
                height: Math.max(draftViewport.height, contentHeight)
                onCursorRectangleChanged: {
                    if (y + cursorRectangle.y < draftViewport.contentY)
                        draftViewport.contentY = Math.max(0, y + cursorRectangle.y)
                    else if (y + cursorRectangle.y + cursorRectangle.height > draftViewport.contentY + draftViewport.height)
                        draftViewport.contentY = y + cursorRectangle.y + cursorRectangle.height - draftViewport.height
                }
                textFormat: TextEdit.PlainText
                wrapMode: root.pointText ? TextEdit.NoWrap : TextEdit.Wrap
                selectByMouse: true
                persistentSelection: true
                color: root.style.color || root.displayedLayer.textColor || "#ffffff"
                selectionColor: "#426e63"
                selectedTextColor: "#ffffff"
                font.family: String(root.style.family || root.displayedLayer.fontFamily || "Helvetica")
                font.pixelSize: Math.max(1, Number(root.style.size || root.displayedLayer.fontSize || 48))
                font.styleName: String(root.style.face || "")
                font.weight: Number(root.style.weight || (root.displayedLayer.fontWeight === "bold" ? 700 : 400))
                font.italic: !!root.style.italic
                font.underline: !!root.style.underline
                font.letterSpacing: Number(root.style.tracking || 0)
                Binding {
                    target: input
                    property: "font"
                    value: textDocumentStyle.font
                    when: root.styledText
                    restoreMode: Binding.RestoreBindingOrValue
                }
                PhotoTextDocumentStyle {
                    id: textDocumentStyle
                    document: input.textDocument
                    style: root.style
                }
                horizontalAlignment: root.displayedLayer.textAlignment === "center" ? TextEdit.AlignHCenter
                    : root.displayedLayer.textAlignment === "right" ? TextEdit.AlignRight
                    : root.displayedLayer.textAlignment === "justify" ? TextEdit.AlignJustify : TextEdit.AlignLeft
                Accessible.name: qsTr("Edit text on canvas")
                Keys.onShortcutOverride: function(event) { event.accepted = true }
                Keys.onEscapePressed: function(event) { root.finish(false); event.accepted = true }
                Keys.onPressed: function(event) {
                    if ((event.modifiers & Qt.ControlModifier || event.modifiers & Qt.MetaModifier)
                            && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                        root.finish(true)
                        event.accepted = true
                    }
                }
            }
        }
    }
    RowLayout {
        visible: root.editing
        x: Math.max(0, Math.min(root.width - width, frame.x))
        y: Math.max(0, Math.min(root.height - height, frame.y + frame.height + 8))
        Label {
            visible: root.editError.length > 0
            text: root.editError
            color: "white"
            wrapMode: Text.WordWrap
            Layout.maximumWidth: 240
        }
        Label {
            objectName: "photoTextDraftStyleNotice"
            visible: root.straightTextDraftNotice
            text: qsTr("Draft shows straight text. Done applies the saved path or material effect.")
            color: theme.textMuted
            wrapMode: Text.WordWrap
            Layout.maximumWidth: 240
            Accessible.name: text
        }
        ActionButton {
            objectName: "photoTextCancelButton"
            text: qsTr("Cancel")
            tone: "quiet"
            compact: true
            tooltipText: qsTr("Cancel text editing and discard these changes.")
            onClicked: root.finish(false)
        }
        ActionButton {
            objectName: "photoTextDoneButton"
            text: qsTr("Done")
            tone: "primary"
            tooltipText: qsTr("Apply the text and finish editing.")
            onClicked: root.finish(true)
        }
    }
}
