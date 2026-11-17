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
    readonly property bool maskTarget: !!controller && controller.editingLayerMask === true
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
    readonly property var authoringSize: {
        const revision = paintCoordinateMapper.geometryRevision
        return controller.photoPaintAuthoringSize ? controller.photoPaintAuthoringSize(layerIndex)
                                                 : Qt.size(canvasWidth, canvasHeight)
    }
    readonly property real documentCanvasWidth: Math.max(1, Number(authoringSize.width || canvasWidth))
    readonly property real documentCanvasHeight: Math.max(1, Number(authoringSize.height || canvasHeight))
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
    readonly property var customizableSettings: ({brushSize: brushSize, hardness: hardness,
        brushOpacity: brushOpacity, flow: flow, brushSpacing: brushSpacing,
        pressureSize: pressureSize, pressureFlow: pressureFlow, textureMaterial: textureMaterial,
        textureStrength: textureStrength, textureScale: textureScale, textureAngle: textureAngle,
        textureSeed: textureSeed, lightFalloff: lightFalloff, lightExposure: lightExposure,
        lightTaper: lightTaper, lightAccumulation: lightAccumulation,
        tipLayerId: tipLayerId, tipPresetName: tipPresetName,
        tipRotationVariation: tipRotationVariation, tipScaleVariation: tipScaleVariation})
    function applyBrushPreset(settings) {
        finish(true)
        const bounds = {brushSize: [1, 4096], hardness: [0, 1], brushOpacity: [0, 1], flow: [0, 1],
            brushSpacing: [0.02, 1], textureStrength: [0, 1], textureScale: [0.5, 256],
            textureAngle: [-180, 180], textureSeed: [0, 4294967295], lightFalloff: [0, 1],
            lightExposure: [-8, 8], lightTaper: [0, 1], tipRotationVariation: [0, 180], tipScaleVariation: [0, 0.9]}
        for (const key of Object.keys(bounds)) {
            if (settings[key] === undefined) continue
            const value = Number(settings[key])
            if (isFinite(value)) root[key] = Math.max(bounds[key][0], Math.min(bounds[key][1], value))
        }
        for (const key of ["pressureSize", "pressureFlow", "lightAccumulation"])
            if (typeof settings[key] === "boolean") root[key] = settings[key]
        if (["solid", "dry_ink", "graphite", "light_paint", "photo_fragment"].indexOf(settings.textureMaterial) >= 0)
            root.textureMaterial = settings.textureMaterial
        for (const key of ["tipLayerId", "tipPresetName"])
            if (typeof settings[key] === "string") root[key] = settings[key]
        refreshCursorTip()
    }
    property var fragmentLayers: []
    property var cursorTipInfo: ({width: 0, height: 0})
    property real cursorDirectionDegrees: 0
    property point cursorDocumentPoint: Qt.point(0, 0)
    function refreshCursorTip() {
        cursorTipInfo = textureMaterial === "photo_fragment" && controller.photoFragmentCursorTipInfo
                ? controller.photoFragmentCursorTipInfo(tipLayerId, tipPresetName) : ({width: 0, height: 0})
    }
    onTextureMaterialChanged: refreshCursorTip()
    onTipLayerIdChanged: refreshCursorTip()
    onTipPresetNameChanged: refreshCursorTip()
    readonly property bool clipSelection: selectionAvailable
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
    readonly property real nominalCursorWidth: Math.max(2, brushSize * imageWidth / documentCanvasWidth)
    readonly property real nominalCursorHeight: Math.max(2, brushSize * imageHeight / documentCanvasHeight)
    readonly property real cursorSizeResponse: tabletDrawing && pressureSize ? Math.max(0.01, cursorPressure) : 1
    readonly property bool editable: ["pixel", "source"].indexOf(String(layerData.kind || "")) >= 0 &&
                                    layerData.locked !== true && layerData.enabled !== false &&
                                    (maskTarget || (layerData.smartObject === undefined && layerData.vectorPath === undefined))
    readonly property bool selectionAvailable: !!root.controller && !!root.controller.hasWorkingSelection
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
            if (root.textureMaterial === "photo_fragment") { root.refreshFragmentLayers(); root.refreshCursorTip() }
        }
    }

    CanvasCoordinateMapper {
        id: paintCoordinateMapper
        contentX: root.imageX
        contentY: root.imageY
        contentWidth: root.imageWidth
        contentHeight: root.imageHeight
        geometryMapper: root.controller
        coordinateSpace: "paint:" + root.targetId
    }
    function point(x, y) {
        const mapped = paintCoordinateMapper.viewToImagePoint(x, y)
        return Qt.point(mapped.x * documentCanvasWidth, mapped.y * documentCanvasHeight)
    }
    function begin(x, y, pressure, penEraser) {
        if (!editable || imageWidth <= 0 || imageHeight <= 0 ||
                x < imageX || y < imageY || x > imageX + imageWidth || y > imageY + imageHeight) return
        root.forceActiveFocus()
        drawing = controller.beginPhotoPaintStroke(layerIndex, {
            color: String(ink), size: brushSize, hardness: hardness, opacity: brushOpacity, flow: flow,
            spacing: brushSpacing,
            erase: eraserTool || erase || penEraser, pressureSize: pressureSize, pressureFlow: pressureFlow,
            textureMaterial: maskTarget || eraserTool || erase || penEraser ? "solid" : textureMaterial, textureStrength: textureStrength,
            textureScale: textureScale, textureAngle: textureAngle, textureSeed: textureSeed,
            lightFalloff: lightFalloff, lightExposure: lightExposure, lightTaper: lightTaper,
            lightAccumulation: lightAccumulation,
            tipLayerId: tipLayerId,
            tipPresetName: tipPresetName,
            tipRotationVariation: tipRotationVariation,
            tipScaleVariation: tipScaleVariation,
            clipSelection: selectionAvailable
        })
        cursorDirectionDegrees = 0
        cursorDocumentPoint = point(x, y)
        move(x, y, pressure)
    }
    function move(x, y, pressure) {
        cursorPoint = Qt.point(x, y)
        cursorPressure = Math.max(0, Math.min(1, pressure))
        if (!drawing) return
        const p = point(x, y)
        const dx = p.x - cursorDocumentPoint.x, dy = p.y - cursorDocumentPoint.y
        if (Math.hypot(dx, dy) > 1e-9) cursorDirectionDegrees = Math.atan2(dy, dx) * 180 / Math.PI
        cursorDocumentPoint = p
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
        cursorShape: projectedPaintCursor.visible && root.nominalCursorWidth >= 8 ? Qt.BlankCursor
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
    Canvas {
        id: projectedPaintCursor
        objectName: "photoPaintCursor"
        enabled: false // Decorative canvas must never take hover from the paint input.
        property int geometryRevision: 0
        readonly property var contours: {
            const revision = geometryRevision
            if (!root.brushCursorVisible || !root.controller.paintCursorContours)
                return ({})
            return root.controller.paintCursorContours(
                        (root.cursorPoint.x - root.imageX) / root.imageWidth,
                        (root.cursorPoint.y - root.imageY) / root.imageHeight,
                        root.brushSize, root.cursorSizeResponse, root.hardness,
                        root.documentCanvasWidth, root.documentCanvasHeight, root.targetId,
                        {textureMaterial: root.textureMaterial, lightFalloff: root.lightFalloff, textureAngle: root.textureAngle,
                         erase: root.eraserTool || root.erase || root.cursorPenEraser,
                         tipWidth: Number(root.cursorTipInfo.width || 0), tipHeight: Number(root.cursorTipInfo.height || 0),
                         tipRotationVariation: root.tipRotationVariation, tipScaleVariation: root.tipScaleVariation,
                         directionDegrees: root.cursorDirectionDegrees})
        }
        readonly property rect contourBounds: {
            const points = (contours.outer || []).concat(contours.nominal || [])
            if (points.length === 0) return Qt.rect(0, 0, 0, 0)
            let left = root.width, top = root.height, right = 0, bottom = 0
            for (let index = 0; index < points.length; ++index) {
                const p = points[index]
                if (!p.valid) continue
                const x = root.imageX + p.x * root.imageWidth
                const y = root.imageY + p.y * root.imageHeight
                left = Math.min(left, x); top = Math.min(top, y)
                right = Math.max(right, x); bottom = Math.max(bottom, y)
            }
            left = Math.max(0, Math.floor(left - 3)); top = Math.max(0, Math.floor(top - 3))
            right = Math.min(root.width, Math.ceil(right + 3)); bottom = Math.min(root.height, Math.ceil(bottom + 3))
            return Qt.rect(left, top, Math.max(0, right - left), Math.max(0, bottom - top))
        }
        x: contourBounds.x
        y: contourBounds.y
        width: contourBounds.width
        height: contourBounds.height
        visible: root.brushCursorVisible && width > 0 && height > 0
        z: 4
        function schedulePaint() { if (visible) requestPaint() }
        function strokeRing(ctx, points, strokeColor, strokeWidth) {
            ctx.beginPath()
            let started = false
            for (let index = 0; index < points.length; ++index) {
                const p = points[index]
                if (!p.valid) { started = false; continue }
                const px = root.imageX + p.x * root.imageWidth - x
                const py = root.imageY + p.y * root.imageHeight - y
                if (!started) { ctx.moveTo(px, py); started = true }
                else ctx.lineTo(px, py)
            }
            ctx.strokeStyle = strokeColor; ctx.lineWidth = strokeWidth; ctx.stroke()
        }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset(); ctx.clearRect(0, 0, width, height)
            if (!visible) return
            strokeRing(ctx, contours.outer || [], "#b3000000", 3)
            strokeRing(ctx, contours.outer || [], root.erase || root.cursorPenEraser ? "#ffd4bf" : "#f4f7fa", 1)
            if (contours.innerVisible === true)
                strokeRing(ctx, contours.inner || [], "#66ffffff", 1)
            if (root.tabletDrawing && root.pressureSize && root.cursorPressure < 0.95)
                strokeRing(ctx, contours.nominal || [], "#55ffffff", 1)
            ctx.fillStyle = "#f4f7fa"
            ctx.fillRect(root.cursorPoint.x - x - 1.5, root.cursorPoint.y - y - 1.5, 3, 3)
        }
        onContoursChanged: schedulePaint()
        onVisibleChanged: schedulePaint()
        onWidthChanged: schedulePaint()
        onHeightChanged: schedulePaint()
        Connections {
            target: root
            function onEraseChanged() { projectedPaintCursor.schedulePaint() }
            function onCursorPenEraserChanged() { projectedPaintCursor.schedulePaint() }
        }
        Connections {
            target: root.controller
            function onControlsChanged() { projectedPaintCursor.geometryRevision += 1 }
            function onPreviewChanged() { projectedPaintCursor.geometryRevision += 1 }
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
        x: root.controlsParent ? 0 : Math.max(0, ((parent ? parent.width : 0) - width) / 2)
        y: root.controlsParent ? 0 : root.toolOptionsTopMargin
        width: root.controlsParent ? Math.max(1, parent ? parent.width : 0)
                                   : Math.max(1, Math.min(theme.scaled(900), Math.max(1, (parent ? parent.width : 0) - theme.space24)))
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
            transientMenu: true
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
                        delegate: ActionButton {
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
                            active: checked
                            autoExclusive: true
                            checked: root.textureMaterial === modelData.key
                            Accessible.name: modelData.label
                            Accessible.description: checked ? qsTr("Selected brush material") : qsTr("Select brush material")
                            onClicked: {
                                photoPaintControls.setBrushMaterial(modelData.key)
                                brushMaterialPopup.closeTransient(true)
                            }
                            AppToolTip {
                                targetItem: materialOption
                                visible: materialOption.hovered
                                text: materialOption.modelData.description
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

        ColumnLayout {
            id: controls
            anchors.fill: parent
            anchors.margins: theme.space8
            spacing: theme.space8

            Flow {
                id: paintCommandRow
                Layout.fillWidth: true
                flow: Flow.LeftToRight
                spacing: theme.space6

            RowLayout {
                id: paintModeControls
                height: implicitHeight
                spacing: theme.space2
                MiniButton {
                    objectName: "photoPaintMode"
                    visible: !root.eraserTool
                    text: root.maskTarget ? qsTr("Reveal") : qsTr("Paint")
                    role: "segmented"
                    active: !root.erase
                    minimumReadableWidth: theme.scaled(58)
                    tooltip: root.maskTarget ? qsTr("Reveal this layer through its mask. X swaps Reveal and Hide.") : qsTr("Paint with the selected brush color.")
                    enabled: root.editable
                    onClicked: root.erase = false
                }
                MiniButton {
                    objectName: "photoPaintEraseMode"
                    visible: !root.eraserTool
                    text: root.maskTarget ? qsTr("Hide") : qsTr("Erase")
                    role: "segmented"
                    active: root.erase
                    minimumReadableWidth: theme.scaled(58)
                    tooltip: root.maskTarget ? qsTr("Hide this layer through its mask; its pixels stay editable.") : qsTr("Erase pixels on the selected layer.")
                    enabled: root.editable
                    onClicked: root.erase = true
                }
                MiniButton {
                    objectName: "photoPaintNewLayerButton"
                    visible: !root.eraserTool && !root.maskTarget
                    square: true
                    iconName: "plus"
                    role: "icon"
                    Accessible.name: qsTr("Create a new paint layer")
                    tooltip: qsTr("Create a new paint layer, then paint on the newly selected layer.")
                    onClicked: root.newLayerRequested()
                }
            }

            RowLayout {
                id: paintColorControls
                visible: !root.eraserTool && !root.maskTarget
                height: implicitHeight
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
                    label: qsTr("Paint color")
                    enabled: !root.erase
                    onPicked: colorDialog.open()
                }
                ToneTextField {
                    objectName: "photoPaintColorValue"
                    Layout.preferredWidth: theme.scaled(94)
                    Layout.preferredHeight: theme.controlHeightDense
                    enabled: !root.erase
                    text: String(root.ink)
                    Accessible.name: qsTr("Brush color value")
                    selectByMouse: true
                    validator: RegularExpressionValidator {
                        regularExpression: /^#?(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
                    }
                    onEditingFinished: {
                        if (acceptableInput)
                            root.ink = text.startsWith("#") ? text : "#" + text
                        text = Qt.binding(() => String(root.ink))
                    }
                }
            }

            ActionButton {
                id: materialChooser
                objectName: "photoPaintMaterialChooser"
                visible: !root.eraserTool && !root.maskTarget
                width: root.controlsParent ? Math.max(1, controls.width) : theme.scaled(176)
                height: theme.controlHeightDense
                padding: theme.space4
                enabled: root.editable
                active: brushMaterialPopup.visible
                onClicked: brushMaterialPopup.toggleTransient(materialChooser)
                Accessible.name: qsTr("Brush material: %1").arg(photoPaintControls.brushMaterialLabel(root.textureMaterial))
                Accessible.description: qsTr("Material preview; stroke appearance depends on settings.")
                AppToolTip {
                    targetItem: materialChooser
                    visible: materialChooser.hovered && !brushMaterialPopup.visible
                    text: qsTr("Choose a brush material. Stroke appearance depends on your settings.")
                }
                contentItem: RowLayout {
                    spacing: theme.space4
                    Canvas {
                        Layout.preferredWidth: theme.scaled(52)
                        Layout.preferredHeight: theme.scaled(22)
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
                    : !root.editable ? (root.layerData.vectorPath !== undefined ? qsTr("Edit this vector with the Pen tool.")
                        : root.layerData.smartObject !== undefined ? qsTr("Use Replace contents or edit this layer’s mask.")
                        : qsTr("Select an unlocked photo or pixel layer in Layers."))
                    : root.drawing ? (root.maskTarget ? (root.erase || root.eraserTool ? qsTr("Hiding…") : qsTr("Revealing…"))
                        : root.eraserTool || root.erase ? qsTr("Erasing…") : qsTr("Painting…"))
                    : root.controller.photoPaintBusy ? qsTr("Finishing stroke…")
                    : root.maskTarget ? (root.erase || root.eraserTool ? qsTr("Hide layer mask") : qsTr("Reveal layer mask"))
                    : qsTr("%1 on %2").arg(root.eraserTool || root.erase ? qsTr("Erase") : qsTr("Paint"))
                        .arg(String(root.layerData.displayName || root.layerData.name || qsTr("selected layer")))
                color: root.controller.photoPaintError.length > 0 ? theme.error : theme.textMuted
                font.family: theme.fontNumeric
                font.features: theme.numericFontFeatures
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
            }

            Label {
                Layout.fillWidth: true
                text: root.eraserTool ? qsTr("Eraser") : qsTr("Brush")
                visible: root.controlsParent === null
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                font.weight: theme.weightControl
            }

            RowLayout {
                visible: !root.maskTarget
                Layout.fillWidth: true
                spacing: theme.space4
                BrushPresetPicker {
                    id: presetPicker
                    objectName: "photoPaintPresets"
                    Layout.fillWidth: true
                    currentSettings: root.customizableSettings
                    onPresetSelected: function(settings) { root.applyBrushPreset(settings) }
                }
                MiniButton {
                    objectName: "photoPaintPreviewToggle"
                    text: qsTr("Preview")
                    role: "segmented"
                    active: presetPicker.previewVisible
                    tooltip: qsTr("Show or hide the tip practice area.")
                    onClicked: presetPicker.previewVisible = !presetPicker.previewVisible
                }
            }

            Flow {
                Layout.fillWidth: true
                visible: !root.eraserTool && !root.maskTarget
                spacing: theme.space2
                Repeater {
                    model: ["#ffffff", "#202020", "#ff6254", "#ffc66d", "#68d5b0", "#78adff"]
                    delegate: ColorSwatchButton {
                        required property string modelData
                        required property int index
                        objectName: "photoPaintQuickColor" + index
                        swatchColor: modelData
                        selected: Qt.colorEqual(root.ink, modelData)
                        label: qsTr("Use brush color %1").arg(modelData)
                        enabled: !root.erase
                        onPicked: function(color) { root.ink = color }
                    }
                }
            }

            GridLayout {
                id: paintQuickControls
                Layout.fillWidth: true
                columns: root.controlsParent ? 1 : (photoPaintControls.width >= theme.scaled(780) ? 4 : 2)
                columnSpacing: theme.space8
                rowSpacing: theme.space8

                ColumnLayout {
                    id: paintSizeControls
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
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
                        ToneTextField {
                            id: paintSizeField
                            objectName: "photoPaintSizeValueField"
                            numericEntry: true
                            Layout.preferredWidth: theme.scaled(72)
                            Layout.preferredHeight: theme.controlHeightDense
                            enabled: root.editable
                            text: String(Math.round(root.brushSize))
                            Accessible.name: qsTr("Brush size in pixels")
                            selectByMouse: true
                            horizontalAlignment: Text.AlignRight
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            validator: IntValidator { bottom: 1; top: 4096 }
                            onEditingFinished: {
                                if (acceptableInput)
                                    root.brushSize = Math.max(1, Math.min(4096, Number(text)))
                                text = Qt.binding(() => String(Math.round(root.brushSize)))
                            }
                            Keys.onEscapePressed: function(event) {
                                text = Qt.binding(() => String(Math.round(root.brushSize)))
                                focus = false
                                event.accepted = true
                            }
                        }
                        Label {
                            text: qsTr("px")
                            color: theme.textMuted
                            font.family: theme.fontNumeric
                            font.features: theme.numericFontFeatures
                            font.pixelSize: theme.typeCaption
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

                InspectorSlider {
                    objectName: "photoPaintHardness"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    controlHeight: theme.controlHeightDense
                    forceCompact: true
                    label: qsTr("Edge hardness")
                    from: 0
                    to: 1
                    stepSize: 0.01
                    value: root.hardness
                    valueText: Math.round(root.hardness * 100) + "%"
                    valueInputMode: "percentNormalized"
                    defaultValue: 0.8
                    enabled: root.editable
                    disabledReason: qsTr("Select an unlocked pixel layer to paint.")
                    onValueDragged: function(nextValue) { root.hardness = nextValue }
                    onResetRequested: valueDragged(defaultValue)
                }

                InspectorSlider {
                    objectName: "photoPaintOpacity"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    controlHeight: theme.controlHeightDense
                    forceCompact: true
                    label: qsTr("Opacity")
                    from: 0
                    to: 1
                    stepSize: 0.01
                    value: root.brushOpacity
                    valueText: Math.round(root.brushOpacity * 100) + "%"
                    valueInputMode: "percentNormalized"
                    defaultValue: 1
                    enabled: root.editable
                    disabledReason: qsTr("Select an unlocked pixel layer to paint.")
                    onValueDragged: function(nextValue) { root.brushOpacity = nextValue }
                    onResetRequested: valueDragged(defaultValue)
                }

                InspectorSlider {
                    objectName: "photoPaintFlow"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    controlHeight: theme.controlHeightDense
                    forceCompact: true
                    label: qsTr("Flow")
                    from: 0
                    to: 1
                    stepSize: 0.01
                    value: root.flow
                    valueText: Math.round(root.flow * 100) + "%"
                    valueInputMode: "percentNormalized"
                    defaultValue: 1
                    enabled: root.editable
                    disabledReason: qsTr("Select an unlocked pixel layer to paint.")
                    onValueDragged: function(nextValue) { root.flow = nextValue }
                    onResetRequested: valueDragged(defaultValue)
                }
            }

            BrushStrokePreview {
                objectName: "photoPaintTipPreview"
                Layout.fillWidth: true
                visible: presetPicker.previewVisible
                diameter: root.brushSize
                hardness: root.hardness
                flow: root.flow
                strokeOpacity: root.brushOpacity
                spacing: root.brushSpacing
                ink: root.ink
                erase: root.eraserTool || root.erase || root.maskTarget
            }


            MiniButton {
                Layout.fillWidth: true
                objectName: "photoPaintAdvancedButton"
                text: root.advancedControlsExpanded ? qsTr("Fewer settings") : qsTr("More settings")
                iconName: root.advancedControlsExpanded ? "chevron-up" : "chevron-down"
                role: "secondary"
                active: root.advancedControlsExpanded
                tooltip: root.eraserTool ? qsTr("Show eraser spacing and tablet pressure controls.")
                                        : qsTr("Show stroke spacing, tablet pressure, texture, and selection controls.")
                onClicked: root.advancedControlsExpanded = !root.advancedControlsExpanded
            }


            RowLayout {
                objectName: "photoPaintSelectionActions"
                visible: root.selectionAvailable
                Layout.fillWidth: true
                spacing: theme.space4
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Selection")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: theme.weightControl
                }
                MiniButton {
                    objectName: "photoPaintEraseSelection"
                    text: qsTr("Erase selected pixels")
                    iconName: "delete"
                    role: "destructive"
                    minimumReadableWidth: theme.scaled(150)
                    enabled: root.editable && root.selectionAvailable && !root.controller.photoPaintBusy
                    disabledReason: root.controller.photoPaintBusy
                        ? qsTr("Finish the active stroke before erasing the selection.")
                        : !root.editable
                            ? qsTr("Select an unlocked photo or pixel layer before erasing pixels.")
                            : qsTr("Create or select a selection before erasing selected pixels.")
                    tooltip: qsTr("Erase pixels inside the active selection from this layer.")
                    onClicked: { root.finish(true); root.controller.erasePixelsFromSelection(root.layerIndex) }
                }
            }

            ColumnLayout {
                objectName: "photoPaintAdvancedControls"
                Layout.fillWidth: true
                visible: root.advancedControlsExpanded
                spacing: theme.space8
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Stroke & response")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: theme.weightControl
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: theme.space8
                    Label {
                        text: qsTr("Material")
                        visible: false
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
                        visible: !root.eraserTool && !root.maskTarget && ["dry_ink", "graphite"].indexOf(root.textureMaterial) >= 0
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
                    visible: !root.eraserTool && !root.maskTarget && ["dry_ink", "graphite"].indexOf(root.textureMaterial) >= 0
                    spacing: theme.space8
                    Column {
                        visible: !root.eraserTool && !root.maskTarget && root.textureMaterial !== "solid"
                        width: theme.scaled(62)
                        spacing: theme.space2
                        Label {
                            text: qsTr("Grain size")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                    ToneTextField {
                        numericEntry: true
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
                        visible: !root.eraserTool && !root.maskTarget && root.textureMaterial !== "solid"
                        width: theme.scaled(58)
                        spacing: theme.space2
                        Label {
                            text: qsTr("Angle")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                    ToneTextField {
                        numericEntry: true
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
                        visible: !root.eraserTool && !root.maskTarget && root.textureMaterial !== "solid"
                        width: theme.scaled(68)
                        spacing: theme.space2
                        Label {
                            text: qsTr("Seed")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                        }
                    ToneTextField {
                        numericEntry: true
                        objectName: "photoPaintTextureSeed"
                        width: theme.scaled(68)
                        height: theme.controlHeightDense
                        text: String(root.textureSeed)
                        Accessible.name: qsTr("Deterministic brush texture seed")
                        validator: DoubleValidator { bottom: 0; top: 4294967295; decimals: 0; notation: DoubleValidator.StandardNotation; locale: "C" }
                        onEditingFinished: if (acceptableInput) root.textureSeed = Number(text)
                    }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space4
                    Label {
                        text: qsTr("Tablet pressure")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: theme.weightControl
                    }
                    Item { Layout.fillWidth: true }
                    MiniButton {
                        objectName: "photoPaintPressureSize"
                        text: qsTr("Size")
                        iconName: "adjust"
                        role: "segmented"
                        active: root.pressureSize
                        Accessible.name: qsTr("Pressure controls brush size")
                        tooltip: qsTr("Use stylus pressure to vary brush size.")
                        onClicked: root.pressureSize = !root.pressureSize
                    }
                    MiniButton {
                        objectName: "photoPaintPressureFlow"
                        text: qsTr("Flow")
                        iconName: "effects"
                        role: "segmented"
                        active: root.pressureFlow
                        Accessible.name: qsTr("Pressure controls brush flow")
                        tooltip: qsTr("Use stylus pressure to vary brush flow.")
                        onClicked: root.pressureFlow = !root.pressureFlow
                    }
                }
                RowLayout {
                    visible: root.selectionAvailable
                    Layout.fillWidth: true
                    spacing: theme.space4
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Selection behavior")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: theme.weightControl
                    }
                    MiniButton {
                        objectName: "photoPaintSelection"
                        text: qsTr("Selection active")
                        iconName: "masks"
                        role: "segmented"
                        active: root.selectionAvailable
                        enabled: root.selectionAvailable
                        disabledReason: qsTr("Create or select a selection before clipping strokes.")
                        tooltip: qsTr("Strokes stay inside the selection. Click to deselect (Ctrl/Cmd+D).")
                        onClicked: root.controller.deselectWorkingSelection()
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    objectName: "photoPaintFragmentControls"
                    visible: !root.eraserTool && !root.maskTarget && root.textureMaterial === "photo_fragment"
                    spacing: theme.space8
                    Label {
                        text: qsTr("Photo fragments")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: theme.weightControl
                    }
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
                    visible: !root.eraserTool && !root.maskTarget && root.textureMaterial === "photo_fragment"
                    spacing: theme.space8
                    Label {
                        text: qsTr("Reusable tip")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: theme.weightControl
                    }
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
                    visible: !root.eraserTool && !root.maskTarget && root.textureMaterial === "light_paint"
                    spacing: theme.space8
                    Label {
                        text: qsTr("Afterglow")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: theme.weightControl
                    }
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
