pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import PhotoEditor

Item {
    id: root
    Theme { id: theme }
    property bool active: false
    property bool panActive: false
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
    property color ink: "#ffffff"
    property bool erase: false
    property bool pressureSize: true
    property bool pressureFlow: false
    property bool clipSelection: false
    property bool drawing: false
    property bool advancedControlsExpanded: false
    property bool tabletDrawing: false
    property point cursorPoint: Qt.point(0, 0)
    property bool cursorVisible: false
    readonly property bool editable: String(layerData.kind || "") === "pixel" &&
                                    layerData.locked !== true && layerData.enabled !== false
    signal newLayerRequested()
    visible: active
    enabled: active && !panActive
    onActiveChanged: finish(true)
    onLayerIndexChanged: finish(false)
    onTargetIdChanged: finish(false)
    onEditableChanged: { if (!editable) finish(false) }
    onDocumentIdChanged: finish(false)
    onPanActiveChanged: { if (panActive) finish(true) }

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
            erase: erase || penEraser, pressureSize: pressureSize, pressureFlow: pressureFlow,
            clipSelection: clipSelection
        })
        move(x, y, pressure)
    }
    function move(x, y, pressure) {
        cursorPoint = Qt.point(x, y)
        if (!drawing) return
        const p = point(x, y)
        controller.addPhotoPaintPoint(p.x, p.y, pressure)
    }
    function finish(commit) {
        if (drawing) controller.finishPhotoPaintStroke(commit)
        drawing = false
        tabletDrawing = false
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
        cursorShape: root.editable ? Qt.CrossCursor : Qt.ArrowCursor
        onEntered: root.cursorVisible = true
        onExited: root.cursorVisible = false
        onPressed: function(mouse) {
            if (mouse.source === Qt.MouseEventNotSynthesized) root.begin(mouse.x, mouse.y, 1, false)
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
            root.begin(x, y, pressure, eraser)
        }
        onTabletMoved: function(x, y, pressure, tiltX, tiltY, eraser) { root.move(x, y, pressure) }
        onTabletReleased: function(x, y, pressure, tiltX, tiltY, eraser) {
            root.move(x, y, pressure)
            root.finish(true)
        }
    }
    Rectangle {
        visible: root.cursorVisible && root.editable
        x: root.cursorPoint.x - width / 2
        y: root.cursorPoint.y - height / 2
        width: Math.max(2, root.brushSize * root.imageWidth / Math.max(1, root.canvasWidth))
        height: Math.max(2, root.brushSize * root.imageHeight / Math.max(1, root.canvasHeight))
        radius: width / 2
        color: "transparent"
        border.color: "white"
        border.width: 1
        Rectangle { anchors.fill: parent; anchors.margins: -1; radius: width / 2; color: "transparent"; border.color: "#80000000" }
    }
    Rectangle {
        objectName: "photoPaintControls"
        readonly property string compactPaintHudContract: "paint-hud:v2|surface:theme-graphite|size:always-visible|advanced:collapsible|width:max-520|shared-controls:true"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: theme.space12
        width: Math.max(theme.scaled(220), Math.min(theme.scaled(520), parent.width - theme.space24))
        height: controls.implicitHeight + theme.space20
        radius: theme.radiusPanel
        color: theme.ink900
        border.color: theme.lineSoft
        border.width: 1
        // Consume presses on the panel so they cannot reach the canvas.
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            id: controls
            anchors.fill: parent
            anchors.margins: theme.panelPaddingCompact
            spacing: theme.gapDense
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.minimumWidth: 0
                    color: theme.textStrong
                    elide: Text.ElideRight
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                    font.weight: Font.DemiBold
                    text: root.editable ? String(root.layerData.name || qsTr("Paint Layer")) : qsTr("Select a paint layer")
                }
                Item { Layout.fillWidth: true }
                MiniButton {
                    objectName: "photoPaintNewLayerButton"
                    text: qsTr("New layer")
                    iconName: "plus"
                    role: "secondary"
                    onClicked: root.newLayerRequested()
                }
                MiniButton {
                    objectName: "photoPaintMode"
                    text: root.erase ? qsTr("Eraser") : qsTr("Brush")
                    iconName: root.erase ? "delete" : "brush-tool"
                    role: "segmented"
                    active: root.erase
                    onClicked: root.erase = !root.erase
                }
                MiniButton {
                    objectName: "photoPaintColorButton"
                    text: qsTr("Color")
                    enabled: !root.erase
                    iconName: "swatch"
                    iconColor: root.ink
                    role: "secondary"
                    onClicked: colorDialog.open()
                }
                MiniButton {
                    objectName: "photoPaintAdvancedButton"
                    text: root.advancedControlsExpanded ? qsTr("Less") : qsTr("More")
                    iconName: root.advancedControlsExpanded ? "chevron-up" : "chevron-down"
                    role: "ghost"
                    active: root.advancedControlsExpanded
                    tooltip: qsTr("Show brush hardness, opacity, flow, and tablet options.")
                    onClicked: root.advancedControlsExpanded = !root.advancedControlsExpanded
                }
            }
            InspectorSlider {
                objectName: "photoPaintSize"
                Layout.fillWidth: true
                label: qsTr("Size")
                from: 0
                to: 1
                stepSize: 0.005
                value: Math.log(Math.max(1, root.brushSize)) / Math.log(4096)
                valueText: Math.round(root.brushSize) + qsTr(" px")
                labelColumnWidth: theme.scaled(56)
                valueColumnWidth: theme.scaled(52)
                resetColumnWidth: 0
                controlHeight: theme.controlHeightDense
                valueInputEnabled: false
                showResetButton: false
                enabled: root.editable
                onValueDragged: function(value) { root.brushSize = Math.max(1, Math.round(Math.pow(4096, value))) }
            }
            ColumnLayout {
                objectName: "photoPaintAdvancedControls"
                Layout.fillWidth: true
                visible: root.advancedControlsExpanded
                spacing: theme.gapDense
                InspectorSlider {
                    objectName: "photoPaintHardness"
                    Layout.fillWidth: true
                    label: qsTr("Hardness")
                    from: 0
                    to: 100
                    stepSize: 1
                    value: root.hardness * 100
                    valueText: Math.round(root.hardness * 100) + "%"
                    labelColumnWidth: theme.scaled(72)
                    valueColumnWidth: theme.scaled(52)
                    resetColumnWidth: 0
                    controlHeight: theme.controlHeightDense
                    valueInputEnabled: false
                    showResetButton: false
                    enabled: root.editable
                    onValueDragged: function(value) { root.hardness = value / 100 }
                }
                InspectorSlider {
                    objectName: "photoPaintOpacity"
                    Layout.fillWidth: true
                    label: qsTr("Opacity")
                    from: 0
                    to: 100
                    stepSize: 1
                    value: root.brushOpacity * 100
                    valueText: Math.round(root.brushOpacity * 100) + "%"
                    labelColumnWidth: theme.scaled(72)
                    valueColumnWidth: theme.scaled(52)
                    resetColumnWidth: 0
                    controlHeight: theme.controlHeightDense
                    valueInputEnabled: false
                    showResetButton: false
                    enabled: root.editable
                    onValueDragged: function(value) { root.brushOpacity = value / 100 }
                }
                InspectorSlider {
                    objectName: "photoPaintFlow"
                    Layout.fillWidth: true
                    label: qsTr("Flow")
                    from: 0
                    to: 100
                    stepSize: 1
                    value: root.flow * 100
                    valueText: Math.round(root.flow * 100) + "%"
                    labelColumnWidth: theme.scaled(72)
                    valueColumnWidth: theme.scaled(52)
                    resetColumnWidth: 0
                    controlHeight: theme.controlHeightDense
                    valueInputEnabled: false
                    showResetButton: false
                    enabled: root.editable
                    onValueDragged: function(value) { root.flow = value / 100 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space4
                    MiniButton {
                        objectName: "photoPaintPressureSize"
                        text: qsTr("Pressure size")
                        role: "segmented"
                        active: root.pressureSize
                        onClicked: root.pressureSize = !root.pressureSize
                    }
                    MiniButton {
                        objectName: "photoPaintPressureFlow"
                        text: qsTr("Pressure flow")
                        role: "segmented"
                        active: root.pressureFlow
                        onClicked: root.pressureFlow = !root.pressureFlow
                    }
                    MiniButton {
                        objectName: "photoPaintSelection"
                        text: qsTr("Clip to selection")
                        role: "segmented"
                        active: root.clipSelection
                        onClicked: root.clipSelection = !root.clipSelection
                    }
                }
            }
            Label {
                objectName: "photoPaintStatusLabel"
                Layout.fillWidth: true
                visible: root.controller.photoPaintError.length > 0 || root.controller.photoPaintBusy
                text: root.controller.photoPaintError.length > 0 ? root.controller.photoPaintError : qsTr("Finishing stroke…")
                color: root.controller.photoPaintError.length > 0 ? theme.error : theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
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
