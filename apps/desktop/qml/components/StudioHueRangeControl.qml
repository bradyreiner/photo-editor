pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: hueRange

    property real sourceHue: 0
    property real targetHue: 0.33
    property real rangeValue: 35
    property real softnessValue: 100
    property color accent: theme.accentWarm400
    property var rainbowColors: ["#d45a62", "#dd8a39", "#d7bd42", "#61b56f", "#49b8c4", "#4e86d9", "#8b6be3", "#c85ac1", "#d45a62"]
    property int editCommitIntervalMs: 16
    property string pendingHueKind: ""
    property real pendingHueValue: 0
    property bool hasPendingHueEdit: false
    readonly property real sourcePosition: clampUnit(sourceHue)
    readonly property real targetPosition: clampUnit(targetHue)

    signal sourceHueEdited(real value)
    signal targetHueEdited(real value)
    signal interactionStarted()
    signal interactionFinished()

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.preferredHeight: 72
    radius: theme.radius8
    color: theme.alpha(theme.glassBase, 0.58)
    border.width: 1
    border.color: theme.alpha(theme.lineSoft, 0.58)
    clip: true

    function clampUnit(value) {
        return Math.max(0, Math.min(1, value))
    }

    function nudgeHue(kind, amount) {
        const current = kind === "source" ? sourceHue : targetHue
        const next = ((current + amount) % 1 + 1) % 1
        interactionStarted()
        queueHueEdited(kind, next)
        flushHueEdited()
        interactionFinished()
    }

    function hueSwatchColor(hue) {
        const wrappedHue = ((hue % 1) + 1) % 1
        return Qt.hsla(wrappedHue, 0.72, 0.58, 1)
    }

    function hueFromX(mouseX) {
        return clampUnit((mouseX - rail.x) / Math.max(1, rail.width))
    }

    function queueHueEdited(kind, value) {
        if (!editCommitTimer.running && !hasPendingHueEdit) {
            if (kind === "source")
                sourceHueEdited(value)
            else
                targetHueEdited(value)
            editCommitTimer.start()
            return
        }
        pendingHueKind = kind
        pendingHueValue = value
        hasPendingHueEdit = true
        if (!editCommitTimer.running)
            editCommitTimer.start()
    }

    function flushHueEdited() {
        if (!hasPendingHueEdit)
            return
        const kind = pendingHueKind
        const value = pendingHueValue
        hasPendingHueEdit = false
        editCommitTimer.stop()
        if (kind === "source")
            sourceHueEdited(value)
        else
            targetHueEdited(value)
    }

    Timer {
        id: editCommitTimer
        interval: hueRange.editCommitIntervalMs
        repeat: false
        onTriggered: hueRange.flushHueEdited()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 7

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: "Hue Mapping"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeLabel
                font.weight: Font.DemiBold
            }

            Label {
                text: Math.round(hueRange.sourceHue * 360) + "\u00B0 -> " + Math.round(hueRange.targetHue * 360) + "\u00B0"
                color: "#C8D2DC"
                font.family: theme.fontMono
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 0
            }
        }

        Item {
            id: rail
            Layout.fillWidth: true
            Layout.preferredHeight: 30

            Row {
                anchors.fill: parent
                spacing: 0
                clip: true

                Repeater {
                    model: hueRange.rainbowColors

                    Rectangle {
                        id: rainbowSegment
                        required property string modelData
                        width: rail.width / Math.max(1, hueRange.rainbowColors.length)
                        height: rail.height
                        color: rainbowSegment.modelData
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                color: theme.alpha(theme.ink950, 0.25)
                border.width: 1
                border.color: theme.alpha(theme.polar, 0.14)
                radius: 3
            }

            Rectangle {
                x: Math.max(0, Math.min(parent.width - width, hueRange.sourcePosition * parent.width - width / 2))
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.max(12, hueRange.rangeValue / 100 * parent.width)
                radius: 3
                color: theme.alpha(hueRange.accent, 0.20 + Math.min(0.35, hueRange.softnessValue / 240))
                border.width: 1
                border.color: theme.alpha(hueRange.accent, 0.46)
            }

            Repeater {
                model: [
                    { kind: "source", position: hueRange.sourcePosition, color: hueRange.hueSwatchColor(hueRange.sourceHue), label: "S" },
                    { kind: "target", position: hueRange.targetPosition, color: hueRange.hueSwatchColor(hueRange.targetHue), label: "T" }
                ]

                Rectangle {
                    id: hueHandle
                    required property var modelData
                    x: Math.max(0, Math.min(parent.width - width, hueHandle.modelData.position * parent.width - width / 2))
                    y: parent.height / 2 - height / 2
                    width: 18
                    height: 18
                    radius: 4
                    rotation: 45
                    color: hueHandle.modelData.color
                    border.width: 2
                    border.color: theme.alpha(theme.ink950, 0.78)
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Slider
                    Accessible.name: hueHandle.modelData.kind === "source" ? qsTr("Source hue") : qsTr("Target hue")
                    Accessible.description: Math.round((hueHandle.modelData.position % 1) * 360) + qsTr(" degrees")
                    Keys.onLeftPressed: hueRange.nudgeHue(String(hueHandle.modelData.kind || ""), -1 / 360)
                    Keys.onRightPressed: hueRange.nudgeHue(String(hueHandle.modelData.kind || ""), 1 / 360)
                    Keys.onDownPressed: hueRange.nudgeHue(String(hueHandle.modelData.kind || ""), -1 / 360)
                    Keys.onUpPressed: hueRange.nudgeHue(String(hueHandle.modelData.kind || ""), 1 / 360)

                    Label {
                        anchors.centerIn: parent
                        rotation: -45
                        text: hueHandle.modelData.label
                        color: theme.ink950
                        font.family: theme.fontUi
                        font.pixelSize: 8
                        font.weight: Font.Black
                    }

                    MouseArea {
                        id: hueHandleMouse
                        anchors.centerIn: parent
                        width: 34
                        height: rail.height
                        hoverEnabled: true
                        cursorShape: Qt.SizeHorCursor
                        onPressed: function(mouse) {
                            hueRange.interactionStarted()
                            const mapped = hueHandleMouse.mapToItem(rail, mouse.x, mouse.y)
                            hueRange.queueHueEdited(String(hueHandle.modelData.kind || ""), hueRange.hueFromX(mapped.x))
                        }
                        onPositionChanged: function(mouse) {
                            if (!hueHandleMouse.pressed)
                                return
                            const mapped = hueHandleMouse.mapToItem(rail, mouse.x, mouse.y)
                            hueRange.queueHueEdited(String(hueHandle.modelData.kind || ""), hueRange.hueFromX(mapped.x))
                        }
                        onReleased: {
                            hueRange.flushHueEdited()
                            hueRange.interactionFinished()
                        }
                        onCanceled: {
                            hueRange.flushHueEdited()
                            hueRange.interactionFinished()
                        }
                    }
                }
            }
        }
    }
}
