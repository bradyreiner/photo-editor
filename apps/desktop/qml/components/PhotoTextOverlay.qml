pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
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
    property Item commandsParent: null
    property real commandsRightInset: 0
    readonly property real commandsHeight: editActionFlow.implicitHeight
    property bool editing: false
    property bool pathHandlesActive: false
    property string editingLayerId: ""
    property int editingLayerIndex: -1
    property string originalText: ""
    property string editError: ""
    property var selectionEditor: input
    readonly property int textSelectionStart: selectionEditor.selectionStart
    readonly property int textSelectionEnd: selectionEditor.selectionEnd
    readonly property bool textSelectionActive: textSelectionStart >= 0 && textSelectionEnd >= 0 &&
        textSelectionStart !== textSelectionEnd
    readonly property string draftText: input.text
    readonly property string textSelectionContent: input.text
    property var creationBox: null
    property var creationViewport: null
    readonly property var displayedLayer: creationBox
        ? Object.assign({kind: "text", id: "draft", text: creationBox.styleText === undefined ? String(creationBox.text || "") : creationBox.styleText, pixelWidth: creationBox.width,
            pixelHeight: creationBox.height, fontSize: creationBox.fontSize || 48},
            {textTypography: creationBox.textTypography, textAlignment: creationBox.textAlignment || "left"}) : layerData
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
    readonly property bool styledText: Number(style.version || 0) >= 1 && Number(style.version || 0) <= 10
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
    onStyleChanged: { baselineCanvas.requestPaint(); recordSessionState() }

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
    property bool editTransactionOpen: false
    property var sessionHistory: []
    property int sessionHistoryPosition: -1
    property bool restoringHistory: false
    property bool finishing: false
    readonly property bool canUndoSession: editing && sessionHistoryPosition > 0
    readonly property bool canRedoSession: editing && sessionHistoryPosition + 1 < sessionHistory.length

    function snapshotStyle() {
        const value = !creationBox && typeof controller.photoLayerTypography === "function"
            ? controller.photoLayerTypography(layerIndex) : style
        const source = String(displayedLayer.text === undefined ? input.text : displayedLayer.text)
        const normalized = typeof controller.photoTextDraftTypography === "function" && source !== input.text
            ? controller.photoTextDraftTypography(value, input.text, ({}), -1, -1, source) : value
        return JSON.parse(JSON.stringify(normalized || ({})))
    }
    function recordSessionState() {
        if (!editing || restoringHistory || finishing || sessionHistoryPosition < 0) return
        const state = {text: input.text, style: snapshotStyle(), alignment: String(displayedLayer.textAlignment || "left")}
        if (JSON.stringify(state) === JSON.stringify(sessionHistory[sessionHistoryPosition])) return
        const entries = sessionHistory.slice(0, sessionHistoryPosition + 1)
        entries.push(state)
        // Bound large copy snapshots; the transaction retains the complete Cancel baseline.
        if (entries.length > 200) entries.splice(1, 1)
        sessionHistory = entries
        sessionHistoryPosition = entries.length - 1
    }
    function startSessionHistory() {
        sessionHistory = [{text: input.text, style: snapshotStyle(), alignment: String(displayedLayer.textAlignment || "left")}]
        sessionHistoryPosition = 0
        selectionEditor = input
    }
    function restoreSessionState(position) {
        if (!editing || position < 0 || position >= sessionHistory.length) return false
        const state = sessionHistory[position]
        restoringHistory = true
        if (creationBox) {
            creationBox = Object.assign({}, creationBox, {textTypography: state.style, textAlignment: state.alignment, styleText: state.text})
        } else if (position === 0) {
            // Restore the exact native baseline, including legacy field absence
            // and point geometry, without traversing another global undo item.
            controller.cancelCoalescedEdit()
            controller.beginCoalescedEdit()
        } else if (typeof controller.restorePhotoLayerTextState === "function") {
            if (!controller.restorePhotoLayerTextState(editingLayerId, state.text, state.style, state.alignment)) {
                restoringHistory = false
                editError = qsTr("Could not restore this text style. Your draft is retained.")
                return false
            }
        } else if (JSON.stringify(snapshotStyle()) !== JSON.stringify(state.style)) {
            restoringHistory = false
            return false
        }
        input.text = state.text
        sessionHistoryPosition = position
        restoringHistory = false
        return true
    }
    function undoSession() { return canUndoSession && restoreSessionState(sessionHistoryPosition - 1) }
    function redoSession() { return canRedoSession && restoreSessionState(sessionHistoryPosition + 1) }
    function updateDraft(text) {
        if (!editing) return false
        if (input.text !== text) input.text = text
        return true
    }
    // Publish copy inside the open session for range styling without ending it.
    function flushPendingText() {
        if (!editing || creationBox) return editing
        if (editingDocumentId !== documentId || editingLayerId !== String(layerData.id)) return false
        if (input.text === String(layerData.text === undefined ? "" : layerData.text)) return true
        const accepted = typeof controller.setPhotoLayerTextById === "function"
            ? controller.setPhotoLayerTextById(editingLayerId, input.text)
            : controller.setPhotoLayerText(layerIndex, input.text)
        if (!accepted) editError = qsTr("Could not save this text. Your draft is retained; retry Done or cancel.")
        return accepted
    }
    function applyDraftTypography(changes, start, end) {
        if (!creationBox || typeof controller.photoTextDraftTypography !== "function") return false
        const styleChanges = Object.assign({}, changes)
        const alignment = styleChanges.alignment === undefined
            ? String(creationBox.textAlignment || "left") : String(styleChanges.alignment)
        delete styleChanges.alignment
        const next = controller.photoTextDraftTypography(style, input.text, styleChanges,
            start === undefined ? -1 : start, end === undefined ? -1 : end,
            creationBox.styleText === undefined ? input.text : creationBox.styleText)
        if (!next || !Object.keys(next).length) return false
        creationBox = Object.assign({}, creationBox, {textTypography: next, textAlignment: alignment, styleText: input.text})
        recordSessionState()
        return true
    }
    function focusCanvas() { selectionEditor = input; input.forceActiveFocus() }
    function handleEditorKey(event) {
        const primary = event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)
        if (primary && event.key === Qt.Key_Z) {
            if (event.modifiers & Qt.ShiftModifier) redoSession()
            else undoSession()
            event.accepted = true
        } else if (primary && event.key === Qt.Key_Y) {
            redoSession()
            event.accepted = true
        } else if (primary && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            finish(true)
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            if (selectionEditor.inputMethodComposing) Qt.inputMethod.reset()
            else finish(false)
            event.accepted = true
        }
    }

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
        controller.beginCoalescedEdit()
        editTransactionOpen = true
        startSessionHistory()
        draftViewport.contentY = 0
        input.forceActiveFocus()
        input.selectAll()
        return true
    }

    function beginEditing() { return beginEditingWithFocus(true) }
    function beginEditingFromInspector() { return beginEditingWithFocus(false) }
    function beginEditingWithFocus(focusInput) {
        if (editing || pathHandlesActive || !active || layerIndex < 0 || !String(layerData.id || "") ||
                layerData.locked || String(layerData.kind) !== "text") return false
        editError = ""
        editingDocumentId = documentId
        editingLayerId = String(layerData.id)
        editingLayerIndex = layerIndex
        if (controller.photoTextEditingLayerId !== undefined)
            controller.photoTextEditingLayerId = editingLayerId
        originalText = layerData.text === undefined ? "Text" : String(layerData.text)
        input.text = originalText
        editing = true
        controller.beginCoalescedEdit()
        editTransactionOpen = true
        startSessionHistory()
        draftViewport.contentY = 0
        if (focusInput !== false) input.forceActiveFocus()
        input.selectAll()
        return true
    }
    function finish(commit) {
        if (!editing) return true
        if (finishing) return false
        finishing = true
        // Settle IME before inspecting the draft. Popup/IME Escape is owned locally.
        if (commit) Qt.inputMethod.commit()
        const created = creationBox !== null
        const sameTarget = editingDocumentId === documentId && (created ||
            editingLayerId === String(layerData.id))
        if (commit && !sameTarget) {
            finishing = false
            editError = qsTr("The text target changed. Your draft is retained; cancel to continue.")
            return false
        }
        const shouldCommit = commit && (!created || input.text.length > 0)
        let accepted = true
        // Prevent controller notifications from treating our own publish as an external edit.
        editing = false
        if (shouldCommit && created) {
            const box = creationBox
            accepted = box.pointText
                ? controller.addPointTextLayer(input.text, box.x, box.y, box.canvasWidth, box.canvasHeight)
                : controller.addParagraphTextLayer(input.text, box.x, box.y,
                    box.width, box.height, box.canvasWidth, box.canvasHeight)
            if (accepted && box.textTypography && typeof controller.restorePhotoLayerTextState === "function") {
                const newLayer = controller.photoLayers[0] || ({})
                accepted = controller.restorePhotoLayerTextState(String(newLayer.id || ""), input.text,
                    controller.photoTextDraftTypography(box.textTypography, input.text, ({}), -1, -1,
                        box.styleText === undefined ? input.text : box.styleText), box.textAlignment || "left")
                if (!accepted) {
                    controller.cancelCoalescedEdit()
                    controller.beginCoalescedEdit()
                }
            } else if (accepted && (box.fontFamily !== undefined || box.fontSize !== undefined || box.color !== undefined)) {
                controller.setPhotoLayerTextStyle(0, box.fontFamily || "Helvetica",
                    Number(box.fontSize || 48), box.color || "#ffffff", "regular", "left")
            }
        } else if (shouldCommit && input.text !== String(layerData.text === undefined ? "" : layerData.text)) {
            accepted = typeof controller.setPhotoLayerTextById === "function"
                ? controller.setPhotoLayerTextById(editingLayerId, input.text)
                : controller.setPhotoLayerText(layerIndex, input.text)
        }
        if (!accepted) {
            editing = true
            finishing = false
            editError = pointGeometry.error ? String(pointGeometry.error)
                : qsTr("Could not save this text. Your draft is retained; retry Done or cancel.")
            selectionEditor.forceActiveFocus()
            return false
        }
        if (editTransactionOpen) {
            editTransactionOpen = false
            if (shouldCommit) controller.endCoalescedEdit()
            else controller.cancelCoalescedEdit()
        }
        input.focus = false
        if (selectionEditor !== input) selectionEditor.focus = false
        if (controller.photoTextEditingLayerId !== undefined)
            controller.photoTextEditingLayerId = ""
        creationBox = null
        creationViewport = null
        sessionHistory = []
        sessionHistoryPosition = -1
        finishing = false
        editFinished(shouldCommit, created)
        return true
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
        if (editing && !creationBox && String(layerData.id) === editingLayerId)
            editingLayerIndex = layerIndex
    }
    onLayerDataChanged: {
        if (pathHandlesActive && (!root.pathPoints.length || root.layerData.locked || root.creationBox))
            pathHandlesActive = false
        if (editing && !creationBox && !restoringHistory) {
            const nextText = String(layerData.text === undefined ? "Text" : layerData.text)
            if (String(layerData.id) !== editingLayerId || layerData.locked) finish(false)
            else if (nextText !== originalText) {
                if (nextText !== input.text) finish(false)
            }
            recordSessionState()
        }
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
            objectName: "photoTextEditingOutline"
            anchors.fill: parent
            color: "transparent"
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
                    sourceText: String(root.displayedLayer.text || "")
                }
                horizontalAlignment: root.displayedLayer.textAlignment === "center" ? TextEdit.AlignHCenter
                    : root.displayedLayer.textAlignment === "right" ? TextEdit.AlignRight
                    : root.displayedLayer.textAlignment === "justify" ? TextEdit.AlignJustify : TextEdit.AlignLeft
                Accessible.name: qsTr("Edit text on canvas")
                Keys.onShortcutOverride: function(event) { event.accepted = true }
                onTextChanged: root.recordSessionState()
                onActiveFocusChanged: if (activeFocus) root.selectionEditor = input
                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) { root.handleEditorKey(event) }
            }
        }
    }
    Flow {
        id: editActionFlow
        objectName: "photoTextEditActions"
        parent: root.commandsParent || root
        visible: root.editing
        width: Math.max(1, Math.min(parent.width - theme.space16 - root.commandsRightInset, theme.scaled(520)))
        height: implicitHeight
        spacing: theme.space4
        x: Math.max(0, (parent.width - root.commandsRightInset - width) / 2)
        y: root.commandsParent ? Math.max(0, (parent.height - height) / 2) : theme.space8
        z: 40
        Label {
            visible: root.editError.length > 0
            width: Math.min(theme.scaled(240), editActionFlow.width)
            text: root.editError
            color: "white"
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "photoTextDraftStyleNotice"
            visible: root.straightTextDraftNotice
            width: Math.min(theme.scaled(240), editActionFlow.width)
            text: qsTr("Path/material applied on Done")
            color: theme.textMuted
            wrapMode: Text.WordWrap
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
