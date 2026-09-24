import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: pad

    property string title: ""
    property bool compact: false
    property string xLabel: ""
    property string yLabel: ""
    property string xValueText: ""
    property string yValueText: ""
    property real xValue: 0
    property real yValue: 0
    property color accent: theme.accentWarm400
    property color leftColor: "#4FB5FF"
    property color rightColor: "#F2C35B"
    property color topColor: "#C85AC1"
    property color bottomColor: "#49B86B"
    property int editCommitIntervalMs: 16
    property real pendingXValue: 0
    property real pendingYValue: 0
    property bool hasPendingValueEdit: false
    readonly property real normalizedX: Math.max(0, Math.min(1, (xValue + 100) / 200))
    readonly property real normalizedY: Math.max(0, Math.min(1, (100 - yValue) / 200))

    property bool dragging: false
    property real startX: 0
    property real startY: 0
    property real dragX: 0
    property real dragY: 0
    property real lastPointerX: 0
    property real lastPointerY: 0
    property int axisLock: 0 // 0: both, 1: horizontal, 2: vertical
    property bool fineMode: false

    function finishDrag(cancel) {
        if (!dragging)
            return
        dragging = false
        if (cancel) {
            editCommitTimer.stop()
            hasPendingValueEdit = false
            valueEdited(startX, startY)
        } else {
            flushValueEdited()
            editCommitTimer.stop()
        }
        interactionFinished()
    }

    Keys.onEscapePressed: function(event) {
        if (dragging) {
            finishDrag(true)
            event.accepted = true
        }
    }

    signal valueEdited(real xValue, real yValue)
    signal interactionStarted()
    signal interactionFinished()

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.preferredHeight: compact ? 120 : 168
    radius: 4
    color: theme.alpha(theme.glassBase, 0.42)
    border.width: 0
    clip: true

    function queueValueEdited(nextXValue, nextYValue) {
        if (!editCommitTimer.running && !hasPendingValueEdit) {
            valueEdited(nextXValue, nextYValue)
            editCommitTimer.start()
            return
        }
        pendingXValue = nextXValue
        pendingYValue = nextYValue
        hasPendingValueEdit = true
        if (!editCommitTimer.running)
            editCommitTimer.start()
    }

    function flushValueEdited() {
        if (!hasPendingValueEdit)
            return
        const nextXValue = pendingXValue
        const nextYValue = pendingYValue
        hasPendingValueEdit = false
        editCommitTimer.stop()
        valueEdited(nextXValue, nextYValue)
    }

    Timer {
        id: editCommitTimer
        interval: pad.editCommitIntervalMs
        repeat: false
        onTriggered: pad.flushValueEdited()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: pad.compact ? 3 : 10
        spacing: 7

        RowLayout {
            visible: !pad.compact
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: pad.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.Black
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0
                elide: Text.ElideNone
            }
            Label {
                text: pad.xValueText + "  /  " + pad.yValueText
                color: "#C8D2DC"
                font.family: theme.fontMono
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 0
            }
        }

        Item {
            id: field
            objectName: "adjustmentPadField"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 94

            Rectangle {
                anchors.fill: parent
                radius: 3
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: pad.leftColor }
                    GradientStop { position: 1.0; color: pad.rightColor }
                }
                opacity: 0.70
            }
            Rectangle {
                anchors.fill: parent
                radius: 3
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.0; color: theme.alpha(pad.topColor, 0.62) }
                    GradientStop { position: 0.50; color: theme.alpha(theme.polar, 0.02) }
                    GradientStop { position: 1.0; color: theme.alpha(pad.bottomColor, 0.62) }
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: 3
                color: theme.alpha(theme.ink950, 0.38)
                border.width: 1
                border.color: theme.alpha(theme.polar, 0.14)
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 1
                color: theme.alpha(theme.polar, 0.24)
            }
            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                width: 1
                color: theme.alpha(theme.polar, 0.24)
            }
            Rectangle {
                x: Math.max(0, Math.min(parent.width - width, pad.normalizedX * parent.width - width / 2))
                y: Math.max(0, Math.min(parent.height - height, pad.normalizedY * parent.height - height / 2))
                width: 18
                height: 18
                radius: 9
                rotation: 0
                color: theme.alpha(theme.polar, 0.94)
                border.width: 2
                border.color: theme.alpha(pad.accent, 0.96)

                Rectangle {
                    anchors.centerIn: parent
                    width: 4
                    height: 4
                    radius: 2
                    color: theme.ink950
                }
            }
            Label {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 8
                anchors.bottomMargin: 6
                text: pad.xLabel
                color: theme.alpha(theme.textStrong, 0.76)
                font.family: theme.fontUi
                font.pixelSize: 9
                font.weight: Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0
            }
            Label {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 8
                anchors.topMargin: 6
                text: pad.yLabel
                color: theme.alpha(theme.textStrong, 0.76)
                font.family: theme.fontUi
                font.pixelSize: 9
                font.weight: Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.CrossCursor
                onPressed: function(mouse) {
                    pad.forceActiveFocus()
                    pad.startX = pad.dragX = pad.xValue
                    pad.startY = pad.dragY = pad.yValue
                    pad.lastPointerX = mouse.x
                    pad.lastPointerY = mouse.y
                    pad.axisLock = 0
                    pad.fineMode = (mouse.modifiers & Qt.ShiftModifier) !== 0
                    pad.dragging = true
                    pad.interactionStarted()
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !pad.dragging)
                        return
                    const dx = mouse.x - pad.lastPointerX
                    const dy = mouse.y - pad.lastPointerY
                    pad.lastPointerX = mouse.x
                    pad.lastPointerY = mouse.y
                    const fine = (mouse.modifiers & Qt.ShiftModifier) !== 0
                    // Re-anchor modifier transitions without applying displacement
                    // accumulated under the previous interaction mode.
                    if (fine !== pad.fineMode) {
                        pad.fineMode = fine
                        return
                    }
                    if ((mouse.modifiers & Qt.ControlModifier) !== 0) {
                        if (pad.axisLock === 0 && (dx !== 0 || dy !== 0))
                            pad.axisLock = Math.abs(dx) >= Math.abs(dy) ? 1 : 2
                    } else {
                        pad.axisLock = 0
                    }
                    const scale = fine ? 0.1 : 1
                    if (pad.axisLock !== 2)
                        pad.dragX = Math.max(-100, Math.min(100, pad.dragX + dx * 200 / field.width * scale))
                    if (pad.axisLock !== 1)
                        pad.dragY = Math.max(-100, Math.min(100, pad.dragY - dy * 200 / field.height * scale))
                    pad.queueValueEdited(pad.dragX, pad.dragY)
                }
                onDoubleClicked: {
                    pad.dragX = 0
                    pad.dragY = 0
                    pad.queueValueEdited(0, 0)
                }
                onReleased: pad.finishDrag(false)
                onCanceled: pad.finishDrag(true)
            }
        }
        Label {
            Layout.fillWidth: true
            visible: !pad.compact
            text: "Shift: fine · Ctrl: lock axis · Esc: cancel"
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 9
            wrapMode: Text.WordWrap
        }
    }
}
