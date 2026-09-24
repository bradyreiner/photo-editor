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

    Rectangle {
        objectName: "photoShapeCreateControls"
        readonly property string shapeHudContract: "shape-hud:v2|surface:theme-graphite|shape-picker:true|color-swatches:true|no-raw-hex:true"
        z: 2
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: theme.space12
        width: Math.max(theme.scaled(300), Math.min(theme.scaled(560), parent.width - theme.space24))
        height: shapeControls.implicitHeight + theme.space16
        radius: theme.radiusPanel
        color: theme.ink900
        border.color: theme.lineSoft
        border.width: 1
        // Keep toolbar input out of the canvas drag gesture.
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: shapeControls
            anchors.fill: parent
            anchors.margins: theme.panelPaddingCompact
            spacing: theme.gapDense

            RowLayout {
                Layout.fillWidth: true
                AppIcon {
                    name: "shape-tool"
                    color: theme.textMain
                    Layout.preferredWidth: theme.scaled(16)
                    Layout.preferredHeight: theme.scaled(16)
                }
                Label {
                    text: qsTr("Shape")
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Drag on the image to draw")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    horizontalAlignment: Text.AlignRight
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6
                ToneComboBox {
                    objectName: "photoShapeTypeControl"
                    Layout.preferredWidth: theme.scaled(164)
                    Layout.minimumWidth: theme.scaled(112)
                    model: [
                        { label: qsTr("Rectangle"), value: "rectangle" },
                        { label: qsTr("Rounded rectangle"), value: "rounded-rectangle" },
                        { label: qsTr("Ellipse"), value: "ellipse" },
                        { label: qsTr("Line"), value: "line" },
                        { label: qsTr("Arrow"), value: "arrow" }
                    ]
                    textRole: "label"
                    currentIndex: Math.max(0, indexForValue(root.shapeType))
                    Accessible.name: qsTr("Shape type")
                    function indexForValue(value) {
                        for (let index = 0; index < model.length; ++index) {
                            if (String(model[index].value) === String(value))
                                return index
                        }
                        return 0
                    }
                    onActivated: function(index) { root.shapeType = String(model[index].value) }
                }
                Label { text: qsTr("Fill"); color: theme.textMuted }
                ColorSwatchButton {
                    objectName: "photoShapeFillColorSwatch"
                    swatchColor: root.fillColor.length > 0 ? root.fillColor : theme.ink750
                    label: root.fillColor.length > 0 ? qsTr("Fill color") : qsTr("Fill is off")
                    enabled: root.enabled && root.shapeType !== "line" && root.shapeType !== "arrow"
                    onPicked: fillColorDialog.open()
                }
                MiniButton {
                    objectName: "photoShapeNoFillButton"
                    text: qsTr("None")
                    role: "segmented"
                    active: root.fillColor.length === 0
                    enabled: root.shapeType !== "line" && root.shapeType !== "arrow"
                    onClicked: root.fillColor = ""
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6
                Label { text: qsTr("Stroke"); color: theme.textMuted }
                ColorSwatchButton {
                    objectName: "photoShapeStrokeColorSwatch"
                    swatchColor: root.strokeColor.length > 0 ? root.strokeColor : theme.ink750
                    label: root.strokeColor.length > 0 ? qsTr("Stroke color") : qsTr("Stroke is off")
                    enabled: root.enabled
                    onPicked: strokeColorDialog.open()
                }
                MiniButton {
                    objectName: "photoShapeNoStrokeButton"
                    text: qsTr("None")
                    role: "segmented"
                    active: root.strokeColor.length === 0
                    onClicked: root.strokeColor = ""
                }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("Width"); color: theme.textMuted }
                ToneTextField {
                    id: strokeWidthField
                    objectName: "photoShapeStrokeWidthField"
                    Layout.preferredWidth: theme.scaled(56)
                    Layout.preferredHeight: theme.controlHeightDense
                    text: Number(root.strokeWidth).toString()
                    Accessible.name: qsTr("Shape stroke width in image pixels")
                    validator: DoubleValidator { bottom: 0; top: 512; decimals: 2; notation: DoubleValidator.StandardNotation; locale: "C" }
                    onTextEdited: if (acceptableInput) root.strokeWidth = Number(text)
                    onEditingFinished: {
                        if (!acceptableInput)
                            text = Number(root.strokeWidth).toString()
                        else
                            text = Qt.binding(function() { return Number(root.strokeWidth).toString() })
                    }
                }
                Label { text: qsTr("px"); color: theme.textMuted }
            }
        }
    }

    Label {
        z: 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: theme.space12
        leftPadding: theme.space10
        rightPadding: theme.space10
        topPadding: theme.space5
        bottomPadding: theme.space5
        text: qsTr("Move, scale, rotate, or restyle the shape from Layers.")
        color: theme.textMain
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        background: Rectangle { color: theme.ink900; border.color: theme.lineSoft; border.width: 1; radius: theme.radiusControl }
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
