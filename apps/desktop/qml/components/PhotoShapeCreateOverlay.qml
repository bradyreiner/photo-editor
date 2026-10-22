pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    Theme { id: theme }
    property bool active: false
    property bool panActive: false
    property Item controlsParent: null
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property int canvasWidth: 0
    property int canvasHeight: 0
    property string shapeType: "rectangle"
    property string fillColor: "#4a78ff"
    property string strokeColor: "#ffffff"
    property real strokeWidth: 4
    property point startPoint: Qt.point(0, 0)
    property point endPoint: Qt.point(0, 0)
    property bool dragging: false
    signal shapeRequested(string type, int startX, int startY, int endX, int endY,
                          int canvasWidth, int canvasHeight,
                          string fillColor, string strokeColor, real strokeWidth)

    visible: active
    enabled: active && !panActive && imageWidth > 0 && imageHeight > 0 &&
             canvasWidth > 0 && canvasHeight > 0
    focus: active
    onActiveChanged: dragging = false
    onStartPointChanged: preview.requestPaint()
    onEndPointChanged: preview.requestPaint()
    onShapeTypeChanged: preview.requestPaint()
    onFillColorChanged: preview.requestPaint()
    onStrokeColorChanged: preview.requestPaint()
    onStrokeWidthChanged: preview.requestPaint()

    function documentPoint(x, y) {
        return Qt.point(Math.max(0, Math.min(canvasWidth, (x - imageX) * canvasWidth / imageWidth)),
                        Math.max(0, Math.min(canvasHeight, (y - imageY) * canvasHeight / imageHeight)))
    }

    function shapeTypeIndex() {
        switch (shapeType) {
        case "rounded-rectangle": return 1
        case "ellipse": return 2
        case "line": return 3
        case "arrow": return 4
        default: return 0
        }
    }

    function finishShape() {
        if (!dragging)
            return
        dragging = false
        const dx = endPoint.x - startPoint.x
        const dy = endPoint.y - startPoint.y
        const minSize = 4 * canvasWidth / imageWidth
        if (Math.hypot(dx, dy) < minSize)
            return
        shapeRequested(shapeType, Math.round(startPoint.x), Math.round(startPoint.y),
                       Math.round(endPoint.x), Math.round(endPoint.y),
                       canvasWidth, canvasHeight, fillColor.trim(), strokeColor.trim(), strokeWidth)
    }

    MouseArea {
        anchors.fill: parent
        z: 0
        enabled: root.enabled
        cursorShape: Qt.CrossCursor
        preventStealing: true
        onPressed: function(mouse) {
            if (mouse.x < root.imageX || mouse.y < root.imageY ||
                    mouse.x > root.imageX + root.imageWidth ||
                    mouse.y > root.imageY + root.imageHeight)
                return
            root.startPoint = root.documentPoint(mouse.x, mouse.y)
            root.endPoint = root.startPoint
            root.dragging = true
        }
        onPositionChanged: function(mouse) {
            if (root.dragging)
                root.endPoint = root.documentPoint(mouse.x, mouse.y)
        }
        onReleased: function(mouse) {
            if (root.dragging)
                root.endPoint = root.documentPoint(mouse.x, mouse.y)
            root.finishShape()
        }
        onCanceled: root.dragging = false
    }

    Canvas {
        id: preview
        anchors.fill: parent
        z: 1
        visible: root.dragging
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!root.dragging || root.canvasWidth <= 0 || root.canvasHeight <= 0)
                return
            const sx = root.imageWidth / root.canvasWidth
            const sy = root.imageHeight / root.canvasHeight
            const x1 = root.imageX + root.startPoint.x * sx
            const y1 = root.imageY + root.startPoint.y * sy
            const x2 = root.imageX + root.endPoint.x * sx
            const y2 = root.imageY + root.endPoint.y * sy
            ctx.beginPath()
            if (root.shapeType === "ellipse") {
                ctx.ellipse((x1 + x2) / 2, (y1 + y2) / 2,
                            Math.max(0.5, Math.abs(x2 - x1) / 2),
                            Math.max(0.5, Math.abs(y2 - y1) / 2), 0, 0, Math.PI * 2)
            } else if (root.shapeType === "line" || root.shapeType === "arrow") {
                ctx.moveTo(x1, y1)
                ctx.lineTo(x2, y2)
            } else {
                ctx.rect(Math.min(x1, x2), Math.min(y1, y2),
                         Math.abs(x2 - x1), Math.abs(y2 - y1))
            }
            ctx.fillStyle = root.fillColor.length > 0 ? root.fillColor : "transparent"
            ctx.strokeStyle = root.strokeColor.length > 0 ? root.strokeColor : theme.primaryAccent
            ctx.lineWidth = Math.max(1, root.strokeWidth * sx)
            if (root.shapeType !== "line" && root.shapeType !== "arrow" && root.fillColor.length > 0)
                ctx.fill()
            if (root.strokeColor.length > 0 && root.strokeWidth > 0)
                ctx.stroke()
            if (root.shapeType === "arrow" && root.strokeColor.length > 0) {
                const angle = Math.atan2(y2 - y1, x2 - x1)
                const head = Math.max(8, Math.min(Math.hypot(x2 - x1, y2 - y1) * 0.35,
                                                   root.strokeWidth * sx * 4))
                ctx.beginPath()
                ctx.moveTo(x2, y2)
                ctx.lineTo(x2 - head * Math.cos(angle - Math.PI / 6),
                           y2 - head * Math.sin(angle - Math.PI / 6))
                ctx.lineTo(x2 - head * Math.cos(angle + Math.PI / 6),
                           y2 - head * Math.sin(angle + Math.PI / 6))
                ctx.closePath()
                ctx.fillStyle = root.strokeColor
                ctx.fill()
            }
        }
    }

    Item {
        id: photoShapeControls
        objectName: "photoShapeCreateControls"
        readonly property string shapeHudContract: "shape-hud:v4|placement:top-tool-rail-or-external-host|surface:transparent|responsive-control-rows:true|stroke-width-horizontal:true"
        parent: root.controlsParent || root
        visible: root.active
        enabled: root.active && !root.panActive
        z: 2
        x: root.controlsParent ? 0 : Math.max(0, (parent.width - width) / 2)
        y: root.controlsParent ? 0 : theme.space6
        width: root.controlsParent ? Math.max(1, parent.width)
                                   : Math.min(theme.scaled(560), Math.max(1, parent.width - theme.space24))
        height: shapeControls.implicitHeight
        function shapeTypeLabel(type) {
            if (type === "rounded-rectangle") return qsTr("Rounded rectangle")
            if (type === "ellipse") return qsTr("Ellipse")
            if (type === "line") return qsTr("Line")
            if (type === "arrow") return qsTr("Arrow")
            return qsTr("Rectangle")
        }
        function setShapeType(type) {
            root.shapeType = type
        }
        function drawShapePreview(ctx, previewWidth, previewHeight, type, fill, stroke, strokeWidth) {
            ctx.clearRect(0, 0, previewWidth, previewHeight)
            const x = previewWidth * 0.16
            const y = previewHeight * 0.18
            const w = previewWidth * 0.68
            const h = previewHeight * 0.64
            const fillValue = String(fill || "")
            const contrast = (stroke.r * 0.2126 + stroke.g * 0.7152 + stroke.b * 0.0722) < 0.3
            ctx.lineWidth = Math.max(1.3, Math.min(3, Number(strokeWidth || 2) * 0.42))
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.strokeStyle = contrast ? theme.textStrong : stroke
            ctx.fillStyle = fillValue.length > 0 ? fillValue : "transparent"
            ctx.beginPath()
            if (type === "ellipse") {
                ctx.ellipse(x + w / 2, y + h / 2, w / 2, h / 2, 0, 0, Math.PI * 2)
            } else if (type === "line" || type === "arrow") {
                ctx.moveTo(x + 2, y + h - 2)
                ctx.lineTo(x + w - 2, y + 2)
            } else if (type === "rounded-rectangle") {
                const r = Math.min(w, h) * 0.2
                ctx.moveTo(x + r, y); ctx.lineTo(x + w - r, y)
                ctx.quadraticCurveTo(x + w, y, x + w, y + r)
                ctx.lineTo(x + w, y + h - r); ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h)
                ctx.lineTo(x + r, y + h); ctx.quadraticCurveTo(x, y + h, x, y + h - r)
                ctx.lineTo(x, y + r); ctx.quadraticCurveTo(x, y, x + r, y)
                ctx.closePath()
            } else {
                ctx.rect(x, y, w, h)
            }
            if (type !== "line" && type !== "arrow" && fillValue.length > 0) ctx.fill()
            ctx.stroke()
            if (type === "arrow") {
                const angle = Math.atan2(-h + 4, w - 4)
                const head = Math.min(9, Math.max(5, h * 0.3))
                ctx.beginPath(); ctx.moveTo(x + w - 2, y + 2)
                ctx.lineTo(x + w - 2 - head * Math.cos(angle - Math.PI / 6),
                           y + 2 - head * Math.sin(angle - Math.PI / 6))
                ctx.moveTo(x + w - 2, y + 2)
                ctx.lineTo(x + w - 2 - head * Math.cos(angle + Math.PI / 6),
                           y + 2 - head * Math.sin(angle + Math.PI / 6))
                ctx.stroke()
            }
        }

        AppPopup {
            id: shapeTypePopup
            objectName: "photoShapeTypePopup"
            parent: Overlay.overlay
            modal: false
            focus: true
            width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(300), Math.max(1, parent ? parent.width - theme.space8 : theme.scaled(300)))))
            height: Math.min(popupAvailableHeight, Math.max(1, Math.min(implicitHeight, parent ? Math.max(1, parent.height - theme.space8) : implicitHeight)))
            x: Math.max(theme.space4, Math.min(parent ? parent.width - width - theme.space4 : 0,
                                                shapeTypeChooser.mapToItem(parent, 0, 0).x))
            y: {
                const anchor = shapeTypeChooser.mapToItem(parent, 0, 0)
                const below = anchor.y + shapeTypeChooser.height + theme.space2
                const above = anchor.y - height - theme.space2
                const maxY = parent ? parent.height - height - theme.space4 : below
                if (below <= maxY) return below
                if (above >= theme.space4) return above
                return Math.max(theme.space4, Math.min(maxY, below))
            }
            padding: theme.space8
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            background: PopupSurface {}
            onOpened: {
                const currentOption = shapeTypeOptions.itemAt(root.shapeTypeIndex())
                if (currentOption) currentOption.forceActiveFocus()
            }
            contentItem: ColumnLayout {
                spacing: theme.space6
                Label {
                    text: qsTr("Choose a shape")
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
                        id: shapeTypeOptions
                        model: [
                            { label: qsTr("Rectangle"), value: "rectangle" },
                            { label: qsTr("Rounded rectangle"), value: "rounded-rectangle" },
                            { label: qsTr("Ellipse"), value: "ellipse" },
                            { label: qsTr("Line"), value: "line" },
                            { label: qsTr("Arrow"), value: "arrow" }
                        ]
                        delegate: Button {
                            id: shapeOption
                            required property var modelData
                            required property int index
                            objectName: "photoShapeTypeOption" + index
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: theme.scaled(88)
                            Layout.preferredHeight: theme.scaled(76)
                            padding: theme.space4
                            checkable: true
                            checked: root.shapeType === modelData.value
                            Accessible.name: modelData.label
                            Accessible.description: checked ? qsTr("Selected shape type") : qsTr("Select shape type")
                            onClicked: {
                                photoShapeControls.setShapeType(modelData.value)
                                shapeTypePopup.close()
                            }
                            background: Rectangle {
                                radius: theme.radiusControl
                                color: shapeOption.activeFocus ? theme.interactionFill
                                       : shapeOption.pressed ? theme.actionSecondaryPressed
                                       : shapeOption.checked ? theme.interactionFill
                                       : shapeOption.hovered ? theme.actionSecondaryHover : theme.surfaceInset
                                border.width: shapeOption.checked || shapeOption.activeFocus
                                              ? theme.focusBorderWidth : theme.hairlineWidth
                                border.color: shapeOption.checked || shapeOption.activeFocus
                                              ? theme.interaction300 : theme.lineSoft
                                Behavior on color { enabled: !theme.reducedMotion; ColorAnimation { duration: theme.durationFast } }
                                Behavior on border.color { enabled: !theme.reducedMotion; ColorAnimation { duration: theme.durationFast } }
                            }
                            contentItem: ColumnLayout {
                                spacing: theme.space2
                                Canvas {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: theme.scaled(32)
                                    property string fill: root.fillColor
                                    property color stroke: root.strokeColor.length > 0 ? root.strokeColor : theme.textMuted
                                    property real strokeWidth: root.strokeWidth
                                    onFillChanged: requestPaint()
                                    onStrokeChanged: requestPaint()
                                    onStrokeWidthChanged: requestPaint()
                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                    onPaint: photoShapeControls.drawShapePreview(getContext("2d"), width, height,
                                        shapeOption.modelData.value, fill, stroke, strokeWidth)
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: shapeOption.modelData.label
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
            id: shapeControls
            width: parent.width
            height: implicitHeight
            spacing: theme.space8

            RowLayout {
                width: root.controlsParent ? shapeControls.width : implicitWidth
                spacing: theme.space6
                AppIcon {
                    name: "shape-tool"
                    color: theme.textMain
                    Layout.preferredWidth: theme.scaled(18)
                    Layout.preferredHeight: theme.scaled(18)
                }
                Label {
                    visible: root.controlsParent !== null
                    text: qsTr("New shape")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    Accessible.name: qsTr("Create a new editable shape layer")
                }
                Button {
                    id: shapeTypeChooser
                    objectName: "photoShapeTypeChooser"
                    Layout.fillWidth: root.controlsParent !== null
                    Layout.preferredWidth: root.controlsParent ? 0 : theme.scaled(220)
                    Layout.minimumWidth: theme.scaled(152)
                    Layout.preferredHeight: theme.controlHeightDense
                    padding: theme.space4
                    onClicked: shapeTypePopup.open()
                    Accessible.name: qsTr("Shape: %1").arg(photoShapeControls.shapeTypeLabel(root.shapeType))
                    Accessible.description: qsTr("Choose a shape to draw.")
                    AppToolTip {
                        id: sharedTooltip1
                        targetItem: shapeTypeChooser
                        text: qsTr("Choose a shape to draw.")
                        visible: shapeTypeChooser.visible && (shapeTypeChooser.hovered)
                        delay: theme.durationSlow
                    }
                    background: Rectangle {
                        radius: theme.radiusControl
                        color: shapeTypeChooser.activeFocus ? theme.interactionFill
                               : shapeTypeChooser.pressed ? theme.actionSecondaryPressed
                               : shapeTypeChooser.hovered ? theme.actionSecondaryHover : theme.actionSecondaryFill
                        border.width: shapeTypeChooser.activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
                        border.color: shapeTypeChooser.activeFocus ? theme.focusRing : theme.controlEdge
                        Behavior on color { enabled: !theme.reducedMotion; ColorAnimation { duration: theme.durationFast } }
                        Behavior on border.color { enabled: !theme.reducedMotion; ColorAnimation { duration: theme.durationFast } }
                    }
                    contentItem: RowLayout {
                        spacing: theme.space4
                        Canvas {
                            Layout.preferredWidth: theme.scaled(30)
                            Layout.preferredHeight: theme.scaled(20)
                            property string shapeType: root.shapeType
                            property string fill: root.fillColor
                            property color stroke: root.strokeColor.length > 0 ? root.strokeColor : theme.textMuted
                            property real strokeWidth: root.strokeWidth
                            onFillChanged: requestPaint()
                            onStrokeChanged: requestPaint()
                            onStrokeWidthChanged: requestPaint()
                            onShapeTypeChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onPaint: photoShapeControls.drawShapePreview(getContext("2d"), width, height,
                                shapeType, fill, stroke, strokeWidth)
                        }
                        Label {
                            Layout.fillWidth: true
                            text: photoShapeControls.shapeTypeLabel(root.shapeType)
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            elide: Text.ElideNone
                            wrapMode: Text.NoWrap
                        }
                        AppIcon {
                            name: "chevron-down"
                            color: theme.textMuted
                            Layout.preferredWidth: theme.scaled(12)
                            Layout.preferredHeight: theme.scaled(12)
                        }
                    }
                }
                ToneComboBox {
                    objectName: "photoShapeTypeControl"
                    visible: false
                    Layout.preferredHeight: theme.controlHeightDense
                    model: [qsTr("Rectangle"), qsTr("Rounded rectangle"), qsTr("Ellipse"), qsTr("Line"), qsTr("Arrow")]
                    currentIndex: root.shapeTypeIndex()
                    Accessible.name: qsTr("Shape type")
                    onActivated: function(index) { root.shapeType = ["rectangle", "rounded-rectangle", "ellipse", "line", "arrow"][index] }
                }
            }

            RowLayout {
                width: root.controlsParent ? shapeControls.width : implicitWidth
                spacing: theme.space6
                Label {
                    text: qsTr("Fill")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                ColorSwatchButton {
                    objectName: "photoShapeFillColorSwatch"
                    swatchColor: root.fillColor.length > 0 ? root.fillColor : theme.ink750
                    selected: root.fillColor.length > 0
                    accent: theme.interaction300
                    label: root.fillColor.length > 0 ? qsTr("Fill color") : qsTr("Fill is off")
                    enabled: root.shapeType !== "line" && root.shapeType !== "arrow"
                    onPicked: fillColorDialog.open()
                }
                MiniButton {
                    objectName: "photoShapeNoFillButton"
                    square: root.controlsParent === null
                    text: root.controlsParent ? qsTr("No fill") : ""
                    iconName: "close"
                    role: root.controlsParent ? "secondary" : "icon"
                    active: root.fillColor.length === 0
                    enabled: root.shapeType !== "line" && root.shapeType !== "arrow"
                    tooltip: qsTr("No fill")
                    onClicked: root.fillColor = ""
                }
            }

            RowLayout {
                width: root.controlsParent ? shapeControls.width : implicitWidth
                spacing: theme.space6
                Label {
                    text: qsTr("Stroke")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                ColorSwatchButton {
                    objectName: "photoShapeStrokeColorSwatch"
                    swatchColor: root.strokeColor.length > 0 ? root.strokeColor : theme.ink750
                    selected: root.strokeColor.length > 0
                    accent: theme.interaction300
                    label: root.strokeColor.length > 0 ? qsTr("Stroke color") : qsTr("Stroke is off")
                    onPicked: strokeColorDialog.open()
                }
                MiniButton {
                    objectName: "photoShapeNoStrokeButton"
                    square: root.controlsParent === null
                    text: root.controlsParent ? qsTr("No stroke") : ""
                    iconName: "close"
                    role: root.controlsParent ? "secondary" : "icon"
                    active: root.strokeColor.length === 0
                    tooltip: qsTr("No stroke")
                    onClicked: root.strokeColor = ""
                }
            }

            ColumnLayout {
                width: root.controlsParent ? shapeControls.width : implicitWidth
                spacing: theme.space2
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Stroke width")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: Math.round(root.strokeWidth) + " px"
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeCaption
                    }
                }
                ToolValueSlider {
                    objectName: "photoShapeStrokeWidthSlider"
                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(112)
                    minimumReadableWidth: theme.scaled(112)
                    Accessible.name: qsTr("Shape stroke width in pixels")
                    tooltipText: qsTr("Stroke width: %1 px").arg(Math.round(root.strokeWidth))
                    from: 0
                    to: 1
                    stepSize: 0.01
                    value: root.strokeWidth <= 0 ? 0 : Math.log(Math.max(1, root.strokeWidth)) / Math.log(512)
                    enabled: root.strokeColor.length > 0
                    onMoved: root.strokeWidth = value <= 0.01 ? 0 : Math.max(1, Math.round(Math.pow(512, value)))
                }
            }
        }
    }

    Label {
        z: 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: theme.space8
        text: qsTr("Move, scale, rotate, or restyle the shape from Layers.")
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        visible: false
    }

    ColorDialog {
        id: fillColorDialog
        title: qsTr("Shape fill")
        selectedColor: root.fillColor.length > 0 ? root.fillColor : "#4a78ff"
        onAccepted: root.fillColor = selectedColor.toString()
    }
    ColorDialog {
        id: strokeColorDialog
        title: qsTr("Shape stroke")
        selectedColor: root.strokeColor.length > 0 ? root.strokeColor : "#ffffff"
        onAccepted: root.strokeColor = selectedColor.toString()
    }

    Keys.onEscapePressed: function(event) {
        if (root.dragging) {
            root.dragging = false
            event.accepted = true
        }
    }
}
