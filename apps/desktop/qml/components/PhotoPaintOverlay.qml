pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import PhotoEditor

Item {
    id: root
    Theme { id: theme }
    FileDialog {
        id: importFragmentPresetDialog
        title: qsTr("Import Photo Fragment preset")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Looksy brush tip (*.looksybrush)"), qsTr("JSON files (*.json)")]
        onAccepted: {
            if (root.controller.importPhotoFragmentBrushPreset(selectedFile.toLocalFile(), false))
                root.refreshFragmentLayers()
        }
    }
    FileDialog {
        id: exportFragmentPresetDialog
        title: qsTr("Export Photo Fragment preset")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "looksybrush"
        nameFilters: [qsTr("Looksy brush tip (*.looksybrush)")]
        onAccepted: root.controller.exportPhotoFragmentBrushPreset(root.tipPresetName, selectedFile.toLocalFile())
    }
    property bool active: false
    property bool panActive: false
    property Item controlsParent: null
    required property var controller
    property var layerData: ({})
    property int layerIndex: -1
    property string documentId: ""
    readonly property string targetId: String(layerData.id || "")
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property int canvasWidth: 0
    property int canvasHeight: 0
    property real brushSize: 32
    property real hardness: 0.8
    property real brushOpacity: 1
    property real flow: 1
    property real brushSpacing: 0.15
    property color ink: "#ffffff"
    property bool erase: false
    property bool eraserTool: false
    property bool settingsForEraser: false
    property var paintSettings: ({})
    property var eraserSettings: ({brushSize: 48, hardness: 0.8, brushOpacity: 1, flow: 1,
        brushSpacing: 0.15, pressureSize: true, pressureFlow: false, advancedControlsExpanded: false})
    function switchToolSettings() {
        const nextIsEraser = root.eraserTool
        if (root.settingsForEraser === nextIsEraser) {
            if (nextIsEraser) root.erase = true
            return
        }
        root.finish(true)
        const keys = ["brushSize", "hardness", "brushOpacity", "flow", "brushSpacing",
                      "pressureSize", "pressureFlow", "advancedControlsExpanded", "erase"]
        const previous = ({})
        for (const key of keys) previous[key] = root[key]
        if (root.settingsForEraser) root.eraserSettings = previous
        else root.paintSettings = previous
        const next = nextIsEraser ? root.eraserSettings : root.paintSettings
        for (const key of keys) if (next[key] !== undefined) root[key] = next[key]
        root.settingsForEraser = nextIsEraser
        if (nextIsEraser) root.erase = true
    }
    onEraserToolChanged: switchToolSettings()
    Component.onCompleted: switchToolSettings()
    property bool pressureSize: true
    property bool pressureFlow: false
    property string textureMaterial: "solid"
    property real textureStrength: 0.45
    property real textureScale: 5
    property real textureAngle: 0
    property double textureSeed: 1
    property real lightFalloff: 0.55
    property real lightExposure: 2
    property real lightTaper: 0
    property bool lightAccumulation: true
    property string tipLayerId: ""
    property string tipPresetName: ""
    property real tipRotationVariation: 0
    property real tipScaleVariation: 0
    property var fragmentLayers: []
    property bool clipSelection: false
    property bool drawing: false
    property bool advancedControlsExpanded: false
    property real toolOptionsTopMargin: theme.space6
    readonly property real toolOptionsHeight: root.visible && root.controlsParent === null ? photoPaintControls.height : 0
    property bool tabletDrawing: false
    property point cursorPoint: Qt.point(0, 0)
    property bool cursorVisible: false
    property real cursorPressure: 1
    property bool cursorPenEraser: false
    readonly property bool cursorInsideImage: imageWidth > 0 && imageHeight > 0 &&
        cursorPoint.x >= imageX && cursorPoint.y >= imageY &&
        cursorPoint.x <= imageX + imageWidth && cursorPoint.y <= imageY + imageHeight
    readonly property bool brushCursorVisible: active && !panActive && editable && cursorVisible && cursorInsideImage
    readonly property real nominalCursorWidth: Math.max(2, brushSize * imageWidth / Math.max(1, canvasWidth))
    readonly property real nominalCursorHeight: Math.max(2, brushSize * imageHeight / Math.max(1, canvasHeight))
    readonly property real cursorSizeResponse: tabletDrawing && pressureSize ? Math.max(0.01, cursorPressure) : 1
    readonly property bool editable: String(layerData.kind || "") === "pixel" &&
                                    layerData.locked !== true && layerData.enabled !== false
    signal newLayerRequested()
    function refreshFragmentLayers() {
        const next = [{ id: "", name: qsTr("Choose copied fragment") }]
        const layers = controller.photoLayerSnapshot ? controller.photoLayerSnapshot() : []
        for (let i = 0; i < layers.length; ++i) {
            const layer = layers[i]
            if (String(layer.kind || "") !== "pixel" || String(layer.id || "") === root.targetId) continue
            next.push({ id: String(layer.id), presetName: "",
                name: String(layer.name || layer.displayName || qsTr("Pixel layer %1").arg(i + 1)) })
        }
        const presets = controller.photoFragmentBrushPresetNames || []
        for (let i = 0; i < presets.length; ++i)
            next.push({ id: "", presetName: String(presets[i]), name: qsTr("Saved · %1").arg(presets[i]) })
        fragmentLayers = next
        let selected = false
        for (let i = 0; i < next.length; ++i)
            if (next[i].id === tipLayerId && next[i].presetName === tipPresetName && (tipLayerId || tipPresetName)) selected = true
        if (!selected) { tipLayerId = ""; tipPresetName = "" }
    }
    onClipSelectionChanged: finish(true)
    visible: active
    enabled: active && !panActive
    onActiveChanged: finish(true)
    onLayerIndexChanged: finish(false)
    onTargetIdChanged: finish(false)
    onEditableChanged: { if (!editable) finish(false) }
    onDocumentIdChanged: finish(false)
    onPanActiveChanged: { if (panActive) finish(true) }
    Connections {
        target: root.controller
        function onControlsChanged() {
            if (root.textureMaterial === "photo_fragment") root.refreshFragmentLayers()
        }
    }

    function point(x, y) {
        // Photo layers composite in the output canvas, after base-image geometry.
        return Qt.point((x - imageX) * canvasWidth / imageWidth,
                        (y - imageY) * canvasHeight / imageHeight)
    }
    function begin(x, y, pressure, penEraser) {
        if (!editable || imageWidth <= 0 || imageHeight <= 0 ||
                x < imageX || y < imageY || x > imageX + imageWidth || y > imageY + imageHeight) return
        root.forceActiveFocus()
        drawing = controller.beginPhotoPaintStroke(layerIndex, {
            color: String(ink), size: brushSize, hardness: hardness, opacity: brushOpacity, flow: flow,
            spacing: brushSpacing,
            erase: eraserTool || erase || penEraser, pressureSize: pressureSize, pressureFlow: pressureFlow,
            textureMaterial: eraserTool ? "solid" : textureMaterial, textureStrength: textureStrength,
            textureScale: textureScale, textureAngle: textureAngle, textureSeed: textureSeed,
            lightFalloff: lightFalloff, lightExposure: lightExposure, lightTaper: lightTaper,
            lightAccumulation: lightAccumulation,
            tipLayerId: tipLayerId,
            tipPresetName: tipPresetName,
            tipRotationVariation: tipRotationVariation,
            tipScaleVariation: tipScaleVariation,
            clipSelection: clipSelection
        })
        move(x, y, pressure)
    }
    function move(x, y, pressure) {
        cursorPoint = Qt.point(x, y)
        cursorPressure = Math.max(0, Math.min(1, pressure))
        if (!drawing) return
        const p = point(x, y)
        controller.addPhotoPaintPoint(p.x, p.y, pressure)
    }
    function finish(commit) {
        if (drawing) controller.finishPhotoPaintStroke(commit)
        drawing = false
        tabletDrawing = false
        cursorPenEraser = false
        cursorPressure = 1
    }
    Keys.onEscapePressed: function(event) {
        if (drawing) finish(false)
        else if (controller.photoPaintBusy) controller.cancelPhotoPaint()
        event.accepted = true
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_BracketLeft || event.key === Qt.Key_BracketRight) {
            brushSize = Math.max(1, Math.min(4096, brushSize * (event.key === Qt.Key_BracketLeft ? 0.8 : 1.25)))
            event.accepted = true
        }
    }
    MouseArea {
        objectName: "photoPaintMouseInput"
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: root.drawing
        cursorShape: root.brushCursorVisible && root.nominalCursorWidth >= 8 ? Qt.BlankCursor
                     : root.editable && root.cursorInsideImage ? Qt.CrossCursor : Qt.ArrowCursor
        onEntered: root.cursorVisible = true
        onExited: root.cursorVisible = false
        onPressed: function(mouse) {
            if (mouse.source === Qt.MouseEventNotSynthesized) {
                root.cursorPenEraser = false
                root.begin(mouse.x, mouse.y, 1, false)
            }
        }
        onPositionChanged: function(mouse) { if (!root.tabletDrawing) root.move(mouse.x, mouse.y, 1) }
        onReleased: function(mouse) {
            if (!root.tabletDrawing) { root.move(mouse.x, mouse.y, 1); root.finish(true) }
        }
        onCanceled: { if (!root.tabletDrawing) root.finish(false) }
    }
    TabletInputArea {
        anchors.fill: parent
        enabled: root.editable
        onTabletPressed: function(x, y, pressure, tiltX, tiltY, eraser) {
            root.tabletDrawing = true
            root.cursorVisible = true
            root.cursorPenEraser = eraser
            root.begin(x, y, pressure, eraser)
        }
        onTabletMoved: function(x, y, pressure, tiltX, tiltY, eraser) {
            root.cursorVisible = true
            root.cursorPenEraser = eraser
            root.move(x, y, pressure)
        }
        onTabletReleased: function(x, y, pressure, tiltX, tiltY, eraser) {
            root.move(x, y, pressure)
            root.finish(true)
        }
    }
    Item {
        objectName: "photoPaintCursor"
        visible: root.brushCursorVisible
        z: 4
        x: root.cursorPoint.x - width / 2
        y: root.cursorPoint.y - height / 2
        width: Math.max(2, root.nominalCursorWidth * root.cursorSizeResponse)
        height: Math.max(2, root.nominalCursorHeight * root.cursorSizeResponse)

        // Two contrasting edges stay legible over both highlights and shadows.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: width / 2
            color: "transparent"
            border.width: 3
            border.color: "#b3000000"
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: root.erase || root.cursorPenEraser ? "#ffd4bf" : "#f4f7fa"
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * root.hardness
            height: parent.height * root.hardness
            radius: width / 2
            visible: root.hardness > 0.05 && root.hardness < 0.95 && parent.width >= 16
            color: "transparent"
            border.width: 1
            border.color: "#66ffffff"
        }
        Rectangle {
            anchors.centerIn: parent
            width: root.nominalCursorWidth
            height: root.nominalCursorHeight
            radius: width / 2
            visible: root.tabletDrawing && root.pressureSize && root.cursorPressure < 0.95
            color: "transparent"
            border.width: 1
            border.color: "#55ffffff"
        }
        Rectangle {
            anchors.centerIn: parent
            width: root.erase || root.cursorPenEraser ? 7 : 3
            height: 3
            radius: 1
            visible: root.nominalCursorWidth >= 8
            color: "#f4f7fa"
            border.width: 1
            border.color: "#90000000"
        }
    }
    Item {
        id: photoPaintControls
        objectName: "photoPaintControls"
        parent: root.controlsParent || root
        visible: root.active
        enabled: root.active && !root.panActive
        readonly property string compactPaintHudContract: "paint-hud:v4|placement:below-viewport-tools|surface:graphite|horizontal-numeric-controls:true|responsive-wrap:true|advanced:collapsible|no-clipping:true"
        function brushMaterialLabel(material) {
            if (material === "dry_ink") return qsTr("Drypoint")
            if (material === "graphite") return qsTr("Graphite")
            if (material === "light_paint") return qsTr("Afterglow")
            if (material === "photo_fragment") return qsTr("Photo fragments")
            return qsTr("Solid")
        }
        function setBrushMaterial(material) {
            root.textureMaterial = material
            if (root.textureMaterial === "photo_fragment") {
                root.refreshFragmentLayers()
                root.advancedControlsExpanded = true
            }
        }
        function drawMaterialPreview(ctx, previewWidth, previewHeight, material, inkColor) {
            ctx.clearRect(0, 0, previewWidth, previewHeight)
            const luminance = inkColor.r * 0.2126 + inkColor.g * 0.7152 + inkColor.b * 0.0722
            ctx.strokeStyle = luminance < 0.36 ? "#e8edf2" : inkColor
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            const y = previewHeight * 0.52
            const left = previewWidth * 0.08
            const right = previewWidth * 0.92
            if (material === "solid") {
                ctx.lineWidth = Math.max(3, previewHeight * 0.14)
                ctx.beginPath(); ctx.moveTo(left, y + 2)
                ctx.bezierCurveTo(previewWidth * 0.34, y - 8, previewWidth * 0.67, y + 8, right, y - 1)
                ctx.stroke()
            } else if (material === "dry_ink") {
                ctx.lineWidth = Math.max(2.5, previewHeight * 0.11)
                ctx.beginPath(); ctx.moveTo(left, y + 4)
                ctx.lineTo(previewWidth * 0.26, y - 5); ctx.lineTo(previewWidth * 0.43, y + 4)
                ctx.lineTo(previewWidth * 0.62, y - 4); ctx.lineTo(previewWidth * 0.8, y + 3)
                ctx.lineTo(right, y - 2); ctx.stroke()
            } else if (material === "graphite") {
                for (let i = 0; i < 4; ++i) {
                    ctx.globalAlpha = 0.32 + i * 0.1
                    ctx.lineWidth = 1.2
                    ctx.beginPath(); ctx.moveTo(left, y - 5 + i * 3)
                    ctx.lineTo(right, y - 4 + i * 2.7); ctx.stroke()
                }
                ctx.globalAlpha = 1
            } else if (material === "light_paint") {
                ctx.beginPath(); ctx.moveTo(left, y + 1)
                ctx.quadraticCurveTo(previewWidth * 0.52, y - 9, right, y)
                ctx.globalAlpha = 0.16; ctx.lineWidth = 12; ctx.stroke()
                ctx.globalAlpha = 0.48; ctx.lineWidth = 6; ctx.stroke()
                ctx.globalAlpha = 1; ctx.lineWidth = 2.5; ctx.stroke()
            } else {
                ctx.lineWidth = Math.max(2.5, previewHeight * 0.11)
                const spans = [[0.08, 0.29], [0.36, 0.55], [0.62, 0.77], [0.83, 0.93]]
                for (let i = 0; i < spans.length; ++i) {
                    const a = spans[i][0], b = spans[i][1]
                    ctx.beginPath(); ctx.moveTo(previewWidth * a, y + (i % 2 ? 3 : -2))
                    ctx.lineTo(previewWidth * ((a + b) / 2), y - 5 + i * 2)
                    ctx.lineTo(previewWidth * b, y + (i % 2 ? -2 : 3)); ctx.stroke()
                }
            }
        }
        x: root.controlsParent ? 0 : Math.max(0, (parent.width - width) / 2)
        y: root.controlsParent ? 0 : root.toolOptionsTopMargin
        width: root.controlsParent ? Math.max(1, parent.width)
                                   : Math.max(1, Math.min(theme.scaled(900), Math.max(1, parent.width - theme.space24)))
        height: controls.implicitHeight + theme.space16
        z: root.controlsParent ? 1 : 5

        Rectangle {
            objectName: "photoPaintToolOptions"
            anchors.fill: parent
            visible: true
            radius: theme.radiusPanel
            color: root.controlsParent ? "transparent" : theme.alpha(theme.surfaceTop, 0.96)
            border.width: root.controlsParent ? 0 : theme.hairlineWidth
            border.color: root.controlsParent ? "transparent" : theme.alpha(theme.glassEdge, 0.72)

            MouseArea {
                anchors.fill: parent
            }
        }

        AppPopup {
            id: brushMaterialPopup
            objectName: "photoPaintMaterialPopup"
            parent: Overlay.overlay
            focus: true
            width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(300), Math.max(1, parent ? parent.width - theme.space8 : theme.scaled(300)))))
            height: Math.min(popupAvailableHeight, Math.max(1, Math.min(implicitHeight, parent ? Math.max(theme.scaled(120), parent.height - theme.space8) : implicitHeight)))
            x: Math.max(theme.space4, Math.min(parent ? parent.width - width - theme.space4 : 0,
                                                materialChooser.mapToItem(parent, 0, 0).x))
            y: {
                const anchorY = materialChooser.mapToItem(parent, 0, materialChooser.height).y + theme.space2
                return Math.max(theme.space4, Math.min(parent ? parent.height - height - theme.space4 : anchorY, anchorY))
            }
            padding: theme.space8
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            onOpened: {
                const currentIndex = root.textureMaterial === "dry_ink" ? 1 : root.textureMaterial === "graphite" ? 2
                    : root.textureMaterial === "light_paint" ? 3 : root.textureMaterial === "photo_fragment" ? 4 : 0
                const currentOption = materialOptionRepeater.itemAt(currentIndex)
                if (currentOption) currentOption.forceActiveFocus()
            }
            background: PopupSurface {}
            contentItem: ColumnLayout {
                spacing: theme.space6
                Label {
                    text: qsTr("Brush material")
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    font.weight: theme.weightControl
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    uniformCellWidths: true
                    columnSpacing: theme.space4
                    rowSpacing: theme.space4
                    Repeater {
                        id: materialOptionRepeater
                        model: [
                            { label: qsTr("Solid"), key: "solid", description: qsTr("Round paint with adjustable edges.") },
                            { label: qsTr("Drypoint"), key: "dry_ink", description: qsTr("Grainy ink that follows your stroke.") },
                            { label: qsTr("Graphite"), key: "graphite", description: qsTr("Soft graphite texture.") },
                            { label: qsTr("Afterglow"), key: "light_paint", description: qsTr("Build up glowing light.") },
                            { label: qsTr("Photo fragments"), key: "photo_fragment", description: qsTr("Stamp copied photo fragments or saved tips.") }
                        ]
                        delegate: Button {
                            id: materialOption
                            required property var modelData
                            required property int index
                            objectName: "photoPaintMaterialOption" + index
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: theme.scaled(88)
                            Layout.preferredHeight: theme.scaled(74)
                            padding: theme.space4
                            checkable: true
                            autoExclusive: true
                            checked: root.textureMaterial === modelData.key
                            Accessible.name: modelData.label
                            Accessible.description: checked ? qsTr("Selected brush material") : qsTr("Select brush material")
                            onClicked: {
                                photoPaintControls.setBrushMaterial(modelData.key)
                                brushMaterialPopup.close()
                            }
                            AppToolTip {
                                targetItem: materialOption
                                visible: materialOption.hovered
                                text: materialOption.modelData.description
                            }
                            background: Rectangle {
                                radius: theme.radiusControl
                                color: materialOption.activeFocus ? theme.interactionFill
                                       : materialOption.pressed ? theme.actionSecondaryPressed
                                       : materialOption.checked ? theme.interactionFill
                                       : materialOption.hovered ? theme.actionSecondaryHover : theme.surfaceInset
                                border.width: root.textureMaterial === materialOption.modelData.key
                                               || materialOption.activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
                                border.color: root.textureMaterial === materialOption.modelData.key
                                              || materialOption.activeFocus ? theme.interaction300 : theme.lineSoft
                                Behavior on color { enabled: !theme.reducedMotion; ColorAnimation { duration: theme.durationFast } }
                                Behavior on border.color { enabled: !theme.reducedMotion; ColorAnimation { duration: theme.durationFast } }
                            }
                            contentItem: ColumnLayout {
                                spacing: theme.space2
                                Canvas {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: theme.scaled(30)
                                    property string materialKey: materialOption.modelData.key
                                    property color inkColor: root.ink
                                    onInkColorChanged: requestPaint()
                                    onMaterialKeyChanged: requestPaint()
                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                    onPaint: photoPaintControls.drawMaterialPreview(getContext("2d"), width, height,
                                                                                     materialKey, inkColor)
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: materialOption.modelData.label
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeMicro
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                }
                            }
                        }
                    }
                }
            }
        }

        Flow {
            id: controls
            anchors.fill: parent
            anchors.margins: theme.space8
            flow: Flow.LeftToRight
            spacing: theme.space6

            RowLayout {
                id: paintModeControls
                height: root.controlsParent ? implicitHeight : paintQuickControls.implicitHeight
                spacing: theme.space2
                MiniButton {
                    objectName: "photoPaintMode"
                    visible: !root.eraserTool
                    text: qsTr("Paint")
                    role: "segmented"
                    active: !root.erase
                    minimumReadableWidth: theme.scaled(58)
                    tooltip: qsTr("Paint with the selected brush color.")
                    enabled: root.editable
                    onClicked: root.erase = false
                }
                MiniButton {
                    objectName: "photoPaintEraseMode"
                    visible: !root.eraserTool
                    text: qsTr("Erase")
                    role: "segmented"
                    active: root.erase
                    minimumReadableWidth: theme.scaled(58)
                    tooltip: qsTr("Erase pixels on the active paint layer.")
                    enabled: root.editable
                    onClicked: root.erase = true
                }
                MiniButton {
                    objectName: "photoPaintNewLayerButton"
                    visible: !root.eraserTool
                    square: true
                    iconName: "plus"
                    role: "icon"
                    Accessible.name: qsTr("Create a new paint layer")
                    tooltip: qsTr("Create a new paint layer, then paint on the newly selected layer.")
                    onClicked: root.newLayerRequested()
                }
                MiniButton {
                    objectName: "photoPaintEraseSelection"
                    square: true
                    iconName: "delete"
                    role: "icon"
                    enabled: root.editable && root.clipSelection && !root.controller.photoPaintBusy
                    tooltip: qsTr("Erase all pixels inside the active selection.")
                    onClicked: { root.finish(true); root.controller.erasePixelsFromSelection(root.layerIndex) }
                }
            }

            RowLayout {
                id: paintColorControls
                visible: !root.eraserTool
                height: root.controlsParent ? implicitHeight : paintQuickControls.implicitHeight
                spacing: theme.space4
                Label {
                    text: qsTr("Color")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: theme.weightControl
                }
                ColorSwatchButton {
                    objectName: "photoPaintColorButton"
                    swatchColor: String(root.ink)
                    label: qsTr("Brush color")
                    enabled: !root.erase
                    onPicked: colorDialog.open()
                }
            }

            Button {
                id: materialChooser
                objectName: "photoPaintMaterialChooser"
                visible: !root.eraserTool
                width: root.controlsParent ? Math.max(1, controls.width) : theme.scaled(176)
                height: theme.controlHeightDense
                padding: theme.space4
                enabled: root.editable
                onClicked: brushMaterialPopup.open()
                Accessible.name: qsTr("Brush material: %1").arg(photoPaintControls.brushMaterialLabel(root.textureMaterial))
                Accessible.description: qsTr("Material preview; stroke appearance depends on settings.")
                AppToolTip {
                    targetItem: materialChooser
                    visible: materialChooser.hovered && !brushMaterialPopup.visible
                    text: qsTr("Choose a brush material. Stroke appearance depends on your settings.")
                }
                background: Rectangle {
                    radius: theme.radiusControl
                    color: materialChooser.activeFocus ? theme.interactionFill
                           : materialChooser.pressed ? theme.actionSecondaryPressed
                           : materialChooser.hovered ? theme.actionSecondaryHover : theme.surfaceInset
                    border.width: theme.hairlineWidth
                    border.color: materialChooser.activeFocus ? theme.interaction300 : theme.lineSoft
                    Behavior on color { enabled: !theme.reducedMotion; ColorAnimation { duration: theme.durationFast } }
                    Behavior on border.color { enabled: !theme.reducedMotion; ColorAnimation { duration: theme.durationFast } }
                }
                contentItem: RowLayout {
                    spacing: theme.space4
                    Canvas {
                        Layout.preferredWidth: theme.scaled(38)
                        Layout.preferredHeight: theme.scaled(20)
                        property string materialKey: root.textureMaterial
                        property color inkColor: root.ink
                        onInkColorChanged: requestPaint()
                        onMaterialKeyChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: photoPaintControls.drawMaterialPreview(getContext("2d"), width, height,
                                                                         materialKey, inkColor)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: photoPaintControls.brushMaterialLabel(root.textureMaterial)
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                        elide: Text.ElideRight
                    }
                    AppIcon {
                        Layout.preferredWidth: theme.scaled(12)
                        Layout.preferredHeight: theme.scaled(12)
                        name: "chevron-down"
                        color: theme.textMuted
                    }
                }
            }

            MiniButton {
                objectName: "photoPaintAdvancedButton"
                text: qsTr("Options")
                iconName: root.advancedControlsExpanded ? "chevron-up" : "chevron-down"
                role: "secondary"
                active: root.advancedControlsExpanded
                tooltip: root.eraserTool ? qsTr("Show eraser pressure, spacing, and selection options.")
                                        : qsTr("Show pressure, spacing, grain, and selection options.")
                onClicked: root.advancedControlsExpanded = !root.advancedControlsExpanded
            }

            Label {
                objectName: "photoPaintStatusLabel"
                // Reserve the space beside Options so starting a stroke never
                // shifts the size/edge controls underneath the user's pointer.
                visible: root.controlsParent !== null || controls.width >= theme.scaled(740)
                width: root.controlsParent ? Math.max(1, controls.width - theme.scaled(110)) : theme.scaled(150)
                height: theme.controlHeightDense
                verticalAlignment: Text.AlignVCenter
                Accessible.name: text
                text: root.controller.photoPaintError.length > 0 ? root.controller.photoPaintError
                    : !root.editable ? qsTr("Select an unlocked pixel layer in Layers.")
                    : root.drawing ? (root.eraserTool ? qsTr("Erasing…") : qsTr("Painting…"))
                    : root.controller.photoPaintBusy ? qsTr("Finishing stroke…")
                    : qsTr("%1 on %2").arg(root.eraserTool || root.erase ? qsTr("Erase") : qsTr("Paint"))
                        .arg(String(root.layerData.displayName || root.layerData.name || qsTr("selected layer")))
                color: root.controller.photoPaintError.length > 0 ? theme.error : theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                wrapMode: root.controlsParent ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                maximumLineCount: root.controlsParent ? 2 : 1
                elide: root.controlsParent ? Text.ElideNone : Text.ElideRight
            }

            Rectangle {
                visible: !root.controlsParent
                width: 1
                height: theme.scaled(30)
                color: theme.lineSoft
            }

            GridLayout {
                id: paintQuickControls
                width: root.controlsParent ? controls.width
                                           : Math.min(theme.scaled(480), Math.max(theme.scaled(416), photoPaintControls.width - theme.scaled(360)))
                columns: root.controlsParent ? 1 : 4
                columnSpacing: theme.space12
                rowSpacing: theme.space8

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(92)
                    spacing: theme.space2
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Size")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: theme.weightControl
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: Math.round(root.brushSize).toString()
                            color: theme.textStrong
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeCaption
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    ToolValueSlider {
                        objectName: "photoPaintSize"
                        Layout.fillWidth: true
                        Layout.minimumWidth: theme.scaled(92)
                        minimumReadableWidth: theme.scaled(92)
                        label: qsTr("Brush size in pixels")
                        tooltipText: qsTr("Brush size: %1 px").arg(Math.round(root.brushSize))
                        from: 0; to: 1; stepSize: 0.005
                        value: Math.log(Math.max(1, root.brushSize)) / Math.log(4096)
                        enabled: root.editable
                        onMoved: root.brushSize = Math.max(1, Math.round(Math.pow(4096, value)))
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(92)
                    spacing: theme.space2
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Edge")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: theme.weightControl
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: Math.round(root.hardness * 100) + "%"
                            color: theme.textStrong
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeCaption
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    ToolValueSlider {
                        objectName: "photoPaintHardness"
                        Layout.fillWidth: true
                        Layout.minimumWidth: theme.scaled(92)
                        minimumReadableWidth: theme.scaled(92)
                        label: qsTr("Brush edge hardness")
                        tooltipText: qsTr("Edge hardness: %1%").arg(Math.round(root.hardness * 100))
                        from: 0; to: 1; stepSize: 0.01; value: root.hardness; enabled: root.editable
                        onMoved: root.hardness = value
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(92)
                    spacing: theme.space2
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Opacity")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: theme.weightControl
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: Math.round(root.brushOpacity * 100) + "%"
                            color: theme.textStrong
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeCaption
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    ToolValueSlider {
                        objectName: "photoPaintOpacity"
                        Layout.fillWidth: true
                        Layout.minimumWidth: theme.scaled(92)
                        minimumReadableWidth: theme.scaled(92)
                        label: qsTr("Brush opacity")
                        tooltipText: qsTr("Opacity: %1%").arg(Math.round(root.brushOpacity * 100))
                        from: 0; to: 1; stepSize: 0.01; value: root.brushOpacity; enabled: root.editable
                        onMoved: root.brushOpacity = value
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(92)
                    spacing: theme.space2
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Flow")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            font.weight: theme.weightControl
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: Math.round(root.flow * 100) + "%"
                            color: theme.textStrong
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeCaption
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    ToolValueSlider {
                        objectName: "photoPaintFlow"
                        Layout.fillWidth: true
                        Layout.minimumWidth: theme.scaled(92)
                        minimumReadableWidth: theme.scaled(92)
                        label: qsTr("Brush flow")
                        tooltipText: qsTr("Flow: %1%").arg(Math.round(root.flow * 100))
                        from: 0; to: 1; stepSize: 0.01; value: root.flow; enabled: root.editable
                        onMoved: root.flow = value
                    }
                }
            }

            ColumnLayout {
                objectName: "photoPaintAdvancedControls"
                width: Math.max(1, root.controlsParent ? controls.width : photoPaintControls.width - theme.space16)
                Layout.alignment: Qt.AlignVCenter
                visible: root.advancedControlsExpanded
                spacing: theme.space8
                Flow {
                    Layout.fillWidth: true
                    spacing: theme.space8
                    Label {
                        text: qsTr("Material")
                        visible: !root.eraserTool
                        height: theme.controlHeightDense
                        verticalAlignment: Text.AlignVCenter
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                    ToneComboBox {
                        objectName: "photoPaintTextureMaterial"
                        visible: false
                        width: theme.scaled(112)
                        height: theme.controlHeightDense
                        model: [qsTr("Solid"), qsTr("Drypoint"), qsTr("Graphite"), qsTr("Afterglow"), qsTr("Photo fragments")]
                        currentIndex: root.textureMaterial === "dry_ink" ? 1 : root.textureMaterial === "graphite" ? 2
                            : root.textureMaterial === "light_paint" ? 3 : root.textureMaterial === "photo_fragment" ? 4 : 0
                        accessibleLabel: qsTr("Brush material")
                        onActivated: function(index) {
                            photoPaintControls.setBrushMaterial(index === 1 ? "dry_ink" : index === 2 ? "graphite"
                                : index === 3 ? "light_paint" : index === 4 ? "photo_fragment" : "solid")
                        }
                    }
                    ToolValueSlider {
                        objectName: "photoPaintSpacing"
                        width: theme.scaled(104)
                        height: theme.controlHeightDense + theme.space24
                        showCaption: true
                        from: 0.02; to: 1; stepSize: 0.01
                        value: root.brushSpacing
                        label: qsTr("Spacing")
                        valueText: Math.round(root.brushSpacing * 100) + "%"
                        onMoved: root.brushSpacing = value
                    }
                    ToolValueSlider {
                        objectName: "photoPaintTextureStrength"
                        visible: !root.eraserTool && root.textureMaterial !== "solid"
                        width: theme.scaled(116)
                        height: theme.controlHeightDense + theme.space24
                        showCaption: true
                        from: 0; to: 1; stepSize: 0.01
                        value: root.textureStrength
                        label: qsTr("Grain")
                        valueText: Math.round(root.textureStrength * 100) + "%"
                        onMoved: root.textureStrength = value
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: theme.space8
                    Column {
                        visible: !root.eraserTool && root.textureMaterial !== "solid"
                        width: theme.scaled(62)
                        spacing: theme.space2
                        Label {
                            text: qsTr("Grain size")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                    ToneTextField {
                        objectName: "photoPaintTextureScale"
                        width: theme.scaled(62)
                        height: theme.controlHeightDense
                        text: String(root.textureScale)
                        Accessible.name: qsTr("Paper grain size in document pixels")
                        validator: DoubleValidator { bottom: 0.5; top: 256; decimals: 1; notation: DoubleValidator.StandardNotation; locale: "C" }
                        onEditingFinished: if (acceptableInput) root.textureScale = Number(text)
                    }
                    }
                    Column {
                        visible: !root.eraserTool && root.textureMaterial !== "solid"
                        width: theme.scaled(58)
                        spacing: theme.space2
                        Label {
                            text: qsTr("Angle")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                    ToneTextField {
                        objectName: "photoPaintTextureAngle"
                        width: theme.scaled(58)
                        height: theme.controlHeightDense
                        text: String(root.textureAngle)
                        Accessible.name: qsTr("Grain angle in degrees")
                        validator: DoubleValidator { bottom: -180; top: 180; decimals: 1; notation: DoubleValidator.StandardNotation; locale: "C" }
                        onEditingFinished: if (acceptableInput) root.textureAngle = Number(text)
                    }
                    }
                    Column {
                        visible: !root.eraserTool && root.textureMaterial !== "solid"
                        width: theme.scaled(68)
                        spacing: theme.space2
                        Label {
                            text: qsTr("Seed")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                    ToneTextField {
                        objectName: "photoPaintTextureSeed"
                        width: theme.scaled(68)
                        height: theme.controlHeightDense
                        text: String(root.textureSeed)
                        Accessible.name: qsTr("Deterministic brush texture seed")
                        validator: DoubleValidator { bottom: 0; top: 4294967295; decimals: 0; notation: DoubleValidator.StandardNotation; locale: "C" }
                        onEditingFinished: if (acceptableInput) root.textureSeed = Number(text)
                    }
                    }
                    MiniButton { objectName: "photoPaintPressureSize"; text: qsTr("Pressure size"); iconName: "adjust"; role: "segmented"; active: root.pressureSize; tooltip: qsTr("Pressure controls brush size."); onClicked: root.pressureSize = !root.pressureSize }
                    MiniButton { objectName: "photoPaintPressureFlow"; text: qsTr("Pressure flow"); iconName: "effects"; role: "segmented"; active: root.pressureFlow; tooltip: qsTr("Pressure controls brush flow."); onClicked: root.pressureFlow = !root.pressureFlow }
                    MiniButton { objectName: "photoPaintSelection"; text: qsTr("Clip to selection"); iconName: "masks"; role: "segmented"; active: root.clipSelection; tooltip: qsTr("Clip painting to the active selection."); onClicked: root.clipSelection = !root.clipSelection }
                }
                Flow {
                    Layout.fillWidth: true
                    objectName: "photoPaintFragmentControls"
                    visible: !root.eraserTool && root.textureMaterial === "photo_fragment"
                    spacing: theme.space8
                    Component.onCompleted: root.refreshFragmentLayers()
                    ToneComboBox {
                        objectName: "photoPaintFragmentSource"
                        width: theme.scaled(180)
                        height: theme.controlHeightDense
                        model: root.fragmentLayers.map(function(layer) { return layer.name })
                        currentIndex: {
                            for (let i = 0; i < root.fragmentLayers.length; ++i)
                                if (root.fragmentLayers[i].id === root.tipLayerId &&
                                        root.fragmentLayers[i].presetName === root.tipPresetName &&
                                        (root.tipLayerId !== "" || root.tipPresetName !== "")) return i
                            return 0
                        }
                        accessibleLabel: qsTr("Photo fragment source layer")
                        onActivated: function(index) {
                            const selected = index >= 0 && index < root.fragmentLayers.length
                                ? root.fragmentLayers[index] : { id: "", presetName: "" }
                            root.tipLayerId = selected.id
                            root.tipPresetName = selected.presetName
                            if (root.tipPresetName !== "") {
                                const settings = root.controller.photoFragmentBrushPreset(root.tipPresetName)
                                root.tipRotationVariation = Number(settings.rotationVariation || 0)
                                root.tipScaleVariation = Number(settings.scaleVariation || 0)
                            }
                        }
                    }
                    ToolValueSlider {
                        objectName: "photoPaintFragmentRotation"
                        width: theme.scaled(160)
                        height: theme.controlHeightDense + theme.space24
                        showCaption: true
                        from: 0; to: 180; stepSize: 1
                        value: root.tipRotationVariation
                        label: qsTr("Rotation drift")
                        valueText: Math.round(root.tipRotationVariation) + "°"
                        onMoved: root.tipRotationVariation = value
                    }
                    ToolValueSlider {
                        objectName: "photoPaintFragmentScale"
                        width: theme.scaled(126)
                        height: theme.controlHeightDense + theme.space24
                        showCaption: true
                        from: 0; to: 0.9; stepSize: 0.01
                        value: root.tipScaleVariation
                        label: qsTr("Size drift")
                        valueText: Math.round(root.tipScaleVariation * 100) + "%"
                        onMoved: root.tipScaleVariation = value
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    objectName: "photoPaintFragmentPresetControls"
                    visible: !root.eraserTool && root.textureMaterial === "photo_fragment"
                    spacing: theme.space8
                    ToneTextField {
                        id: fragmentPresetName
                        objectName: "photoPaintFragmentPresetName"
                        width: theme.scaled(160)
                        height: theme.controlHeightDense
                        placeholderText: qsTr("Name this fragment")
                        Accessible.name: qsTr("Photo Fragment preset name")
                        maximumLength: 64
                    }
                    MiniButton {
                        objectName: "photoPaintFragmentPresetSave"
                        square: true; iconName: "save"; role: "icon"
                        enabled: fragmentPresetName.text.trim().length > 0 &&
                            (root.tipLayerId !== "" || root.tipPresetName !== "")
                        tooltip: qsTr("Save the selected fragment as a reusable preset.")
                        onClicked: {
                            if (root.controller.savePhotoFragmentBrushPreset(fragmentPresetName.text,
                                    root.tipLayerId, root.tipPresetName,
                                    root.tipRotationVariation, root.tipScaleVariation, true)) {
                                root.tipLayerId = ""
                                root.tipPresetName = fragmentPresetName.text.trim()
                                root.refreshFragmentLayers()
                            }
                        }
                    }
                    MiniButton {
                        objectName: "photoPaintFragmentPresetImport"
                        square: true; iconName: "folder"; role: "icon"
                        tooltip: qsTr("Import a transferable Photo Fragment preset.")
                        onClicked: importFragmentPresetDialog.open()
                    }
                    MiniButton {
                        objectName: "photoPaintFragmentPresetExport"
                        square: true; iconName: "export"; role: "icon"
                        enabled: root.tipPresetName !== ""
                        tooltip: qsTr("Export the selected Photo Fragment preset.")
                        onClicked: exportFragmentPresetDialog.open()
                    }
                    MiniButton {
                        objectName: "photoPaintFragmentPresetDelete"
                        square: true; iconName: "delete"; role: "icon"
                        enabled: root.tipPresetName !== ""
                        tooltip: qsTr("Delete the selected Photo Fragment preset.")
                        onClicked: {
                            if (root.controller.deletePhotoFragmentBrushPreset(root.tipPresetName)) {
                                root.tipPresetName = ""
                                root.refreshFragmentLayers()
                            }
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    objectName: "photoPaintLightControls"
                    visible: !root.eraserTool && root.textureMaterial === "light_paint"
                    spacing: theme.space8
                    ToolValueSlider {
                        objectName: "photoPaintLightFalloff"
                        width: theme.scaled(130)
                        height: theme.controlHeightDense + theme.space24
                        showCaption: true
                        from: 0; to: 1; stepSize: 0.01
                        value: root.lightFalloff
                        label: qsTr("Halo")
                        valueText: Math.round(root.lightFalloff * 100) + "%"
                        onMoved: root.lightFalloff = value
                    }
                    ToolValueSlider {
                        objectName: "photoPaintLightTaper"
                        width: theme.scaled(124)
                        height: theme.controlHeightDense + theme.space24
                        showCaption: true
                        from: 0; to: 1; stepSize: 0.01
                        value: root.lightTaper
                        label: qsTr("Taper")
                        valueText: Math.round(root.lightTaper * 100) + "%"
                        onMoved: root.lightTaper = value
                    }
                    ToolValueSlider {
                        objectName: "photoPaintLightExposure"
                        width: theme.scaled(140)
                        height: theme.controlHeightDense + theme.space24
                        showCaption: true
                        from: -8; to: 8; stepSize: 0.1
                        value: root.lightExposure
                        label: qsTr("Exposure")
                        valueText: (root.lightExposure > 0 ? "+" : "") + root.lightExposure.toFixed(1) + " EV"
                        onMoved: root.lightExposure = value
                    }
                    MiniButton {
                        objectName: "photoPaintLightAccumulation"
                        square: true
                        iconName: "effects"
                        role: "icon"
                        active: root.lightAccumulation
                        tooltip: root.lightAccumulation ? qsTr("Overlapping dabs build brightness.")
                            : qsTr("Overlapping dabs keep the brightest coverage.")
                        onClicked: root.lightAccumulation = !root.lightAccumulation
                    }
                }
            }
        }
    }
    ColorDialog {
        id: colorDialog
        title: qsTr("Brush color")
        selectedColor: root.ink
        onAccepted: root.ink = selectedColor
    }
}
