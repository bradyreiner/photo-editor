pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import "." as Components

Item {
    id: root

    Theme { id: theme }
    property bool active: false
    property Item controlsParent: null
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 0
    property real imageHeight: 0
    property int canvasWidth: 0
    property int canvasHeight: 0
    property point startPoint: Qt.point(0, 0)
    property point endPoint: Qt.point(0, 0)
    property bool dragging: false
    property var availableFonts: []
    property string creationText: "Text"
    property string creationFontFamily: "Helvetica"
    property real creationFontSize: 48
    property color creationColor: "#ffffff"
    signal boxRequested(int x, int y, int width, int height)
    signal pointRequested(int x, int y)
    signal textRequested(var request)
    visible: active
    enabled: active && imageWidth > 0 && imageHeight > 0 && canvasWidth > 0 && canvasHeight > 0
    onActiveChanged: dragging = false

    function documentPoint(x, y) {
        return Qt.point(Math.max(0, Math.min(canvasWidth, (x - imageX) * canvasWidth / imageWidth)),
                        Math.max(0, Math.min(canvasHeight, (y - imageY) * canvasHeight / imageHeight)))
    }

    function emitTextRequest(x, y, width, height, pointText) {
        textRequested({
            x: x,
            y: y,
            width: width,
            height: height,
            pointText: pointText,
            canvasWidth: canvasWidth,
            canvasHeight: canvasHeight,
            text: creationText,
            fontFamily: String(creationFontFamily || "Helvetica").trim(),
            fontSize: Math.max(1, Math.min(8192, creationFontSize)),
            color: creationColor.toString()
        })
    }

    function finishBox() {
        if (!dragging) return
        dragging = false
        const dx = Math.abs(endPoint.x - startPoint.x)
        const dy = Math.abs(endPoint.y - startPoint.y)
        const click = dx * imageWidth / canvasWidth < 4 && dy * imageHeight / canvasHeight < 4
        if (click) {
            const x = Math.round(startPoint.x)
            const y = Math.round(startPoint.y)
            pointRequested(x, y)
            emitTextRequest(x, y, 512, 128, true)
            return
        }
        const x = Math.round(Math.min(startPoint.x, endPoint.x))
        const y = Math.round(Math.min(startPoint.y, endPoint.y))
        const width = Math.max(16, Math.min(8192, Math.round(dx)))
        const height = Math.max(16, Math.min(8192, Math.round(dy)))
        boxRequested(x, y, width, height)
        emitTextRequest(x, y, width, height, false)
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.IBeamCursor
        onPressed: function(mouse) {
            if (!root.enabled)
                return
            if (mouse.x < root.imageX || mouse.y < root.imageY ||
                    mouse.x > root.imageX + root.imageWidth || mouse.y > root.imageY + root.imageHeight) return
            root.startPoint = root.documentPoint(mouse.x, mouse.y)
            root.endPoint = root.startPoint
            root.dragging = true
        }
        onPositionChanged: function(mouse) {
            if (root.dragging) root.endPoint = root.documentPoint(mouse.x, mouse.y)
        }
        onReleased: function(mouse) {
            if (root.dragging) root.endPoint = root.documentPoint(mouse.x, mouse.y)
            root.finishBox()
        }
        onCanceled: root.dragging = false
    }
    Rectangle {
        visible: root.dragging
        x: root.imageX + Math.min(root.startPoint.x, root.endPoint.x) * root.imageWidth / root.canvasWidth
        y: root.imageY + Math.min(root.startPoint.y, root.endPoint.y) * root.imageHeight / root.canvasHeight
        width: Math.abs(root.endPoint.x - root.startPoint.x) * root.imageWidth / root.canvasWidth
        height: Math.abs(root.endPoint.y - root.startPoint.y) * root.imageHeight / root.canvasHeight
        color: theme.alpha(theme.primaryAccent, 0.12)
        border.color: theme.primaryAccent
        border.width: 1
    }

    Item {
        id: creationControls
        objectName: "photoTextCreateControls"
        parent: root.controlsParent || root
        visible: root.active
        enabled: root.active
        z: 3
        x: root.controlsParent ? 0 : Math.max(theme.space8, (parent.width - width) / 2)
        y: root.controlsParent ? 0 : theme.space8
        width: root.controlsParent ? Math.max(1, parent.width)
                                    : Math.min(theme.scaled(360), Math.max(1, parent.width - theme.space16))
        height: creationContent.implicitHeight + theme.space16

        Rectangle {
            anchors.fill: parent
            radius: theme.radiusPanel
            color: root.controlsParent ? "transparent" : theme.alpha(theme.surfaceTop, 0.97)
            border.width: root.controlsParent ? 0 : theme.hairlineWidth
            border.color: root.controlsParent ? "transparent" : theme.alpha(theme.glassEdge, 0.76)

            MouseArea {
                anchors.fill: parent
            }
        }

        ColumnLayout {
            id: creationContent
            anchors.fill: parent
            anchors.margins: theme.space8
            spacing: theme.space6

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6
                Components.AppIcon {
                    name: "text-tool"
                    color: theme.textStrong
                    Layout.preferredWidth: theme.scaled(16)
                    Layout.preferredHeight: theme.scaled(16)
                }
                Label {
                    text: qsTr("NEW TEXT")
                    color: theme.textStrong
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeMicro
                    font.weight: Font.DemiBold
                    Accessible.name: qsTr("New text layer")
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("DRAFT")
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeMicro
                }
            }

            Components.ToneTextArea {
                id: textInput
                objectName: "photoTextCreationInput"
                Layout.fillWidth: true
                Layout.preferredHeight: theme.scaled(68)
                text: root.creationText
                placeholderText: qsTr("Write something…")
                accessibleLabel: qsTr("Text to create")
                font.family: root.creationFontFamily
                font.pixelSize: theme.typeControl
                onTextChanged: if (root.creationText !== text) root.creationText = text
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6
                Label {
                    text: qsTr("Font")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                Components.PhotoFontPicker {
                    id: fontControl
                    objectName: "photoTextCreationFontPicker"
                    triggerObjectName: "photoTextCreationFont"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: theme.controlHeightDense
                    availableFonts: root.availableFonts
                    currentFamily: root.creationFontFamily
                    enabled: root.active
                    onFamilySelected: function(family) {
                        root.creationFontFamily = family
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6
                Label {
                    text: qsTr("Size")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                }
                Components.ToneSpinBox {
                    id: sizeControl
                    objectName: "photoTextCreationSize"
                    from: 1
                    to: 8192
                    value: Math.round(root.creationFontSize)
                    editable: true
                    Layout.fillWidth: true
                    Layout.preferredHeight: theme.controlHeightDense
                    Accessible.name: qsTr("Text size in pixels")
                    valueUnit: qsTr("pixels")
                    onValueModified: root.creationFontSize = value
                }
                Label {
                    text: qsTr("px")
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeCaption
                }
                Components.ColorSwatchButton {
                    objectName: "photoTextCreationColor"
                    swatchColor: root.creationColor.toString()
                    label: qsTr("Text color: %1").arg(root.creationColor.toString())
                    onPicked: colorDialog.open()
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Click for point text · drag for a paragraph box")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
                Accessible.name: text
            }
        }
    }

    ColorDialog {
        id: colorDialog
        title: qsTr("Text color")
        selectedColor: root.creationColor
        onAccepted: root.creationColor = selectedColor
    }
}
