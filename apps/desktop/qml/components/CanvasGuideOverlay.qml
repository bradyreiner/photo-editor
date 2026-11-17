pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    required property CanvasGuideState guideState
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: width
    property real imageHeight: height
    property bool contentReady: true
    readonly property real rail: 26
    readonly property bool railsShown: guideState.rulersVisible && contentReady
    readonly property real scaleX: imageWidth / Math.max(1, guideState.canvasWidth)
    readonly property real scaleY: imageHeight / Math.max(1, guideState.canvasHeight)
    property bool dragging: false
    property int dragIndex: -1
    property string dragAxis: "x"
    property real dragPosition: 0
    property bool dragOutside: false
    clip: true

    Keys.onEscapePressed: function(event) {
        event.accepted = dragging;
        dragging = false;
    }
    onContentReadyChanged: if (!contentReady)
        dragging = false

    Theme {
        id: theme
    }

    function beginGuide(index, axis, area, mouse) {
        if (guideState.locked)
            return;
        forceActiveFocus();
        dragging = true;
        dragIndex = index;
        dragAxis = axis;
        updateGuide(area, mouse);
    }

    function updateGuide(area, mouse) {
        if (!dragging)
            return;
        const point = area.mapToItem(root, mouse.x, mouse.y);
        const vertical = dragAxis === "x";
        const position = vertical ? (point.x - imageX) / scaleX : (point.y - imageY) / scaleY;
        dragPosition = guideState.quantize(position, vertical, Boolean(mouse.modifiers & Qt.ShiftModifier));
        const limit = vertical ? guideState.canvasWidth : guideState.canvasHeight;
        dragOutside = point.x < (railsShown ? rail : 0) || point.y < (railsShown ? rail : 0) || point.x > width || point.y > height || dragPosition < 0 || dragPosition > limit;
    }

    function finishGuide() {
        if (!dragging)
            return;
        if (!dragOutside || dragIndex >= 0)
            guideState.commitGuide(dragIndex, dragAxis, dragPosition, dragOutside);
        dragging = false;
    }

    component GuideLine: Item {
        id: line
        required property int guideIndex
        required property string axis
        required property real position
        readonly property bool vertical: axis === "x"
        // Keep the grabbed hit area stationary; only the separate preview moves.
        x: vertical ? root.imageX + position * root.scaleX - 4 : Math.max(root.railsShown ? root.rail : 0, root.imageX)
        y: vertical ? Math.max(root.railsShown ? root.rail : 0, root.imageY) : root.imageY + position * root.scaleY - 4
        width: vertical ? 9 : Math.max(0, Math.min(root.width, root.imageX + root.imageWidth) - x)
        height: vertical ? Math.max(0, Math.min(root.height, root.imageY + root.imageHeight) - y) : 9
        visible: root.contentReady && root.guideState.guidesVisible && (vertical ? x >= (root.railsShown ? root.rail - 4 : -4) && x < root.width : y >= (root.railsShown ? root.rail - 4 : -4) && y < root.height)
        Rectangle {
            visible: line.guideIndex === -1 || !root.dragging || root.dragIndex !== line.guideIndex
            anchors.centerIn: parent
            width: line.vertical ? 1 : parent.width
            height: line.vertical ? parent.height : 1
            color: root.dragging && root.dragIndex === line.guideIndex && root.dragOutside ? theme.error : theme.opticalLight
        }
        MouseArea {
            id: guidePointer
            objectName: "canvasGuidePointer" + line.guideIndex
            anchors.fill: parent
            enabled: !root.guideState.locked && line.guideIndex >= 0
            hoverEnabled: true
            preventStealing: true
            cursorShape: line.vertical ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: function (mouse) {
                root.beginGuide(line.guideIndex, line.axis, guidePointer, mouse);
            }
            onPositionChanged: function (mouse) {
                if (pressed)
                    root.updateGuide(guidePointer, mouse);
            }
            onReleased: function (mouse) {
                root.updateGuide(guidePointer, mouse);
                root.finishGuide();
            }
            onCanceled: root.dragging = false
            AppToolTip {
                targetItem: guidePointer
                visible: guidePointer.containsMouse && !guidePointer.pressed
                text: root.guideState.label(line.position, line.vertical) + " · Drag to move; drag to a rail to remove. Shift: multiples of 5."
            }
        }
    }

    Repeater {
        model: root.guideState.guides
        delegate: GuideLine {
            required property var modelData
            required property int index
            guideIndex: index
            axis: String(modelData.axis)
            position: Number(modelData.position)
        }
    }
    GuideLine {
        guideIndex: -1
        axis: root.dragAxis
        position: root.dragPosition
        visible: root.dragging
    }

    component RulerRail: Rectangle {
        id: ruler
        required property bool vertical
        x: vertical ? 0 : root.rail
        y: vertical ? root.rail : 0
        width: vertical ? root.rail : Math.max(0, root.width - root.rail)
        height: vertical ? Math.max(0, root.height - root.rail) : root.rail
        color: theme.ink900
        visible: root.railsShown
        Canvas {
            id: ticks
            anchors.fill: parent
            readonly property real origin: (ruler.vertical ? root.imageY : root.imageX) - root.rail
            readonly property real unitScale: (ruler.vertical ? root.scaleY : root.scaleX) * root.guideState.pixelsPerUnit(!ruler.vertical)
            readonly property string unit: root.guideState.unit
            onOriginChanged: requestPaint()
            onUnitScaleChanged: requestPaint()
            onUnitChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                if (!(unitScale > 0))
                    return;
                const length = ruler.vertical ? height : width;
                const rawStep = 70 / unitScale;
                const power = Math.pow(10, Math.floor(Math.log10(rawStep)));
                const ratio = rawStep / power;
                const step = (ratio <= 1 ? 1 : ratio <= 2 ? 2 : ratio <= 5 ? 5 : 10) * power;
                const minor = step / 5;
                const first = Math.ceil(-origin / (minor * unitScale));
                const count = Math.min(2000, Math.ceil(length / (minor * unitScale)) + 2);
                ctx.strokeStyle = theme.textMuted;
                ctx.fillStyle = theme.textMuted;
                ctx.font = "10px \"" + theme.fontUi + "\"";
                for (let i = first; i < first + count; ++i) {
                    const pixel = origin + i * minor * unitScale;
                    const major = ((i % 5) + 5) % 5 === 0;
                    ctx.beginPath();
                    if (ruler.vertical) {
                        ctx.moveTo(width, pixel);
                        ctx.lineTo(width - (major ? 9 : 4), pixel);
                    } else {
                        ctx.moveTo(pixel, height);
                        ctx.lineTo(pixel, height - (major ? 9 : 4));
                    }
                    ctx.stroke();
                    if (major) {
                        const decimals = Math.max(0, Math.min(8, -Math.floor(Math.log10(step))));
                        const label = String(Number((i * minor).toFixed(decimals)));
                        ctx.save();
                        if (ruler.vertical) {
                            ctx.translate(10, pixel + 3);
                            ctx.rotate(-Math.PI / 2);
                            ctx.fillText(label, 0, 0);
                        } else
                            ctx.fillText(label, pixel + 3, 11);
                        ctx.restore();
                    }
                }
            }
        }
        MouseArea {
            id: railPointer
            objectName: ruler.vertical ? "canvasLeftRuler" : "canvasTopRuler"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            hoverEnabled: true
            preventStealing: true
            cursorShape: root.guideState.locked ? Qt.ArrowCursor : ruler.vertical ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: function (mouse) {
                if (mouse.button === Qt.RightButton)
                    settingsPopup.open();
                else {
                    root.guideState.guidesVisible = true;
                    root.beginGuide(-1, ruler.vertical ? "x" : "y", railPointer, mouse);
                }
            }
            onPositionChanged: function (mouse) {
                if (pressed)
                    root.updateGuide(railPointer, mouse);
            }
            onReleased: function (mouse) {
                root.updateGuide(railPointer, mouse);
                root.finishGuide();
            }
            onCanceled: root.dragging = false
            AppToolTip {
                targetItem: railPointer
                visible: railPointer.containsMouse && !railPointer.pressed
                text: "Drag out a " + (ruler.vertical ? "vertical" : "horizontal") + " guide. Shift: multiples of 5 " + root.guideState.unit + ". Right-click for units and guide settings."
            }
        }
    }
    RulerRail {
        vertical: false
    }
    RulerRail {
        vertical: true
    }

    Rectangle {
        visible: root.railsShown
        width: root.rail
        height: root.rail
        color: theme.ink900
        Text {
            anchors.centerIn: parent
            text: root.guideState.unit
            color: theme.textStrong
            font.pixelSize: 10
        }
        MouseArea {
            id: unitSettingsPointer
            anchors.fill: parent
            onClicked: settingsPopup.open()
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            AppToolTip {
                targetItem: unitSettingsPointer
                visible: unitSettingsPointer.containsMouse
                text: "Ruler units and guides · Ctrl/⌘+Shift+R: rulers · Ctrl/⌘+;: guides"
            }
        }
    }
    Rectangle {
        visible: root.dragging
        x: Math.max(root.rail, Math.min(root.width - width, root.imageX + (root.dragAxis === "x" ? root.dragPosition * root.scaleX : 12)))
        y: root.rail + 8
        width: dragLabel.implicitWidth + 16
        height: 26
        radius: 4
        color: theme.ink900
        Text {
            id: dragLabel
            anchors.centerIn: parent
            text: root.dragOutside ? "Release to remove" : root.guideState.label(root.dragPosition, root.dragAxis === "x")
            color: theme.textStrong
            font.pixelSize: 12
        }
    }

    AppPopup {
        id: settingsPopup
        objectName: "canvasGuideSettingsPopup"
        x: root.rail
        y: root.rail
        width: Math.min(320, popupAvailableWidth)
        height: Math.min(popupAvailableHeight, guideControls.implicitHeight + padding * 2)
        contentItem: Flickable {
            id: settingsScroll
            contentHeight: guideControls.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: guideControls
                width: settingsScroll.width
                spacing: 8
                Text {
                    text: "Rulers & guides"
                    color: theme.textStrong
                    font.weight: Font.DemiBold
                }
                ToneComboBox {
                    objectName: "canvasGuideUnitPicker"
                    Layout.fillWidth: true
                    model: ["Pixels", "Inches", "Centimeters", "Percentage"]
                    currentIndex: root.guideState.unit === "px" ? 0 : root.guideState.unit === "in" ? 1 : root.guideState.unit === "cm" ? 2 : 3
                    onActivated: root.guideState.unit = ["px", "in", "cm", "%"][currentIndex]
                    Accessible.name: "Ruler measurement unit"
                }
                RowLayout {
                    Text {
                        text: "Pixels per inch"
                        color: theme.textMuted
                        Layout.fillWidth: true
                    }
                    ToneSpinBox {
                        objectName: "canvasGuidePpi"
                        from: 1
                        to: 2400
                        editable: true
                        value: root.guideState.pixelsPerInch
                        onValueModified: root.guideState.pixelsPerInch = value
                        accessibleLabel: "Ruler pixels per inch"
                    }
                }
                ToneCheckBox {
                    objectName: "canvasShowRulersCheckBox"
                    text: "Show rulers"
                    checked: root.guideState.rulersVisible
                    onToggled: root.guideState.rulersVisible = checked
                }
                ToneCheckBox {
                    text: "Show guides"
                    checked: root.guideState.guidesVisible
                    onToggled: root.guideState.guidesVisible = checked
                }
                ToneCheckBox {
                    text: "Snap objects to guides"
                    checked: root.guideState.snappingEnabled
                    onToggled: root.guideState.snappingEnabled = checked
                }
                ToneCheckBox {
                    text: "Lock guides"
                    checked: root.guideState.locked
                    onToggled: root.guideState.locked = checked
                }
                Text {
                    Layout.fillWidth: true
                    text: "Drag from the top or left rail. Hold Shift for multiples of 5 in the selected unit. Drag a guide back to a rail to remove it.\n\nCtrl/⌘+Shift+R: rulers\nCtrl/⌘+;: guides\nGuides are saved locally for this document."
                    wrapMode: Text.WordWrap
                    color: theme.textMuted
                    font.pixelSize: 12
                }
                MiniButton {
                    text: "Clear guides"
                    enabled: !root.guideState.locked && root.guideState.guides.length > 0
                    onClicked: root.guideState.clear()
                }
            }
        }
    }
}
