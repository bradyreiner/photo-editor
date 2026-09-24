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
    property string editingLayerId: ""
    property int editingLayerIndex: -1
    property string originalText: ""
    property string editError: ""
    property var creationBox: null
    property var creationViewport: null
    readonly property var displayedLayer: creationBox
        ? ({kind: "text", pixelWidth: creationBox.width, pixelHeight: creationBox.height, fontSize: 48}) : layerData
    readonly property var baseFrame: creationBox && creationViewport ? ({
        frameX: creationViewport.imageX + creationBox.x * creationViewport.imageWidth / creationBox.canvasWidth,
        frameY: creationViewport.imageY + creationBox.y * creationViewport.imageHeight / creationBox.canvasHeight,
        frameWidth: creationBox.width * creationViewport.imageWidth / creationBox.canvasWidth,
        frameHeight: creationBox.height * creationViewport.imageHeight / creationBox.canvasHeight,
        layerRotation: 0
    }) : transformFrame
    readonly property var style: root.displayedLayer.textTypography || (creationBox ? ({
        version: 2, mode: creationBox.pointText ? "point" : "paragraph", paragraphSpacing: 0,
        family: "Helvetica", face: "", size: 48, weight: 400,
        italic: false, underline: false, tracking: 0, lineHeight: 1.2,
        baselineOffset: 0, color: "#ffffffff"
    }) : ({}))
    readonly property bool styledText: Number(style.version || 0) === 1 || Number(style.version || 0) === 2
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

    function beginCreation(box, viewport) {
        if (editing || !active || box.width < 16 || box.height < 16 ||
                box.canvasWidth <= 0 || box.canvasHeight <= 0) return false
        editingDocumentId = documentId
        creationBox = box
        creationViewport = viewport
        originalText = ""
        editError = ""
        input.text = ""
        editing = true
        draftViewport.contentY = 0
        input.forceActiveFocus()
        return true
    }

    function beginEditing() {
        if (editing || !active || layerIndex < 0 || !String(layerData.id || "") ||
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
            accepted = box.pointText
                ? controller.addPointTextLayer(input.text, box.x, box.y, box.canvasWidth, box.canvasHeight)
                : controller.addParagraphTextLayer(input.text, box.x, box.y,
                    box.width, box.height, box.canvasWidth, box.canvasHeight)
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
    onDocumentIdChanged: { if (editing) finish(false) }
    onActiveChanged: { if (!active) finish(false) }
    onLayerIndexChanged: { if (editing && !creationBox) finish(false) }
    onLayerDataChanged: {
        if (editing && !creationBox && (String(layerData.id) !== editingLayerId || layerData.locked ||
                String(layerData.text === undefined ? "Text" : layerData.text) !== originalText)) finish(false)
    }
    visible: editing && active
    enabled: visible

    MouseArea {
        objectName: "photoCanvasTextOutsideArea"
        anchors.fill: parent
        onClicked: root.finish(true)
    }

    Item {
        id: frame
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
        x: Math.max(0, Math.min(root.width - width, frame.x))
        y: Math.max(0, Math.min(root.height - height, frame.y + frame.height + 8))
        Label {
            visible: root.editError.length > 0
            text: root.editError
            color: "white"
            wrapMode: Text.WordWrap
            Layout.maximumWidth: 240
        }
        Button { text: qsTr("Cancel"); onClicked: root.finish(false) }
        Button { text: qsTr("Done"); onClicked: root.finish(true) }
    }
}
