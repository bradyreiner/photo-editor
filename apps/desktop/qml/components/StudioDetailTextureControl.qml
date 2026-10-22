pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: detailControl

    property real sharpening: 0
    property real radiusValue: 1
    property real detailValue: 0
    property real masking: 0
    property real luminanceNr: 0
    property real colorNr: 0
    property color accent: theme.accentWarm400
    property int editCommitIntervalMs: 16
    property var pendingValues: ({})
    property var pendingKeys: []
    readonly property real normalizedRadius: clamp((radiusValue - 0.5) / 2.5, 0, 1)
    readonly property real normalizedSharpen: clamp(sharpening / 100, 0, 1)

    signal valueEdited(string key, real value)
    signal interactionStarted()
    signal interactionFinished()

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.preferredHeight: 154
    radius: 4
    color: theme.alpha(theme.glassBase, 0.42)
    border.width: 0
    clip: true

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function queueValueEdited(key, value) {
        if (!editCommitTimer.running && pendingKeys.length === 0) {
            valueEdited(key, value)
            editCommitTimer.start()
            return
        }
        const nextValues = Object.assign({}, pendingValues)
        const nextKeys = pendingKeys.slice()
        if (!Object.prototype.hasOwnProperty.call(nextValues, key))
            nextKeys.push(key)
        nextValues[key] = value
        pendingValues = nextValues
        pendingKeys = nextKeys
        if (!editCommitTimer.running)
            editCommitTimer.start()
    }

    function flushValueEdits() {
        if (pendingKeys.length === 0)
            return
        const keys = pendingKeys
        const values = pendingValues
        pendingKeys = []
        pendingValues = ({})
        editCommitTimer.stop()
        for (let i = 0; i < keys.length; ++i)
            valueEdited(keys[i], values[keys[i]])
    }

    function editFocus(mouseX, mouseY) {
        const nextRadius = 0.5 + clamp(mouseX / Math.max(1, focusPad.width), 0, 1) * 2.5
        const nextAmount = clamp(100 - mouseY / Math.max(1, focusPad.height) * 100, 0, 100)
        queueValueEdited("sharpenRadius", nextRadius)
        queueValueEdited("sharpening", nextAmount)
    }

    function editRail(key, mouseY, railHeight) {
        queueValueEdited(key, clamp(100 - mouseY / Math.max(1, railHeight) * 100, 0, 100))
    }

    Timer {
        id: editCommitTimer
        interval: detailControl.editCommitIntervalMs
        repeat: false
        onTriggered: detailControl.flushValueEdits()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 7

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: "Texture Response"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.Black
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0
            }

            Label {
                text: Math.round(detailControl.sharpening) + " / " +
                      detailControl.radiusValue.toFixed(1) + "px / " +
                      Math.round(detailControl.masking)
                color: "#C8D2DC"
                font.family: theme.fontMono
                font.pixelSize: 9
                font.weight: Font.Bold
                font.letterSpacing: 0
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Item {
                id: focusPad
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 92

                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: theme.alpha("#4FB5FF", 0.28) }
                        GradientStop { position: 1.0; color: theme.alpha("#F2C35B", 0.56) }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: theme.alpha(detailControl.accent, 0.50) }
                        GradientStop { position: 1.0; color: theme.alpha(theme.ink950, 0.82) }
                    }
                    border.width: 1
                    border.color: theme.alpha(theme.polar, 0.14)
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Math.max(2, detailControl.detailValue / 100 * parent.height)
                    color: theme.alpha(theme.polar, 0.16)
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 1
                    color: theme.alpha(theme.polar, 0.20)
                }
                Rectangle {
                    x: Math.max(0, Math.min(parent.width - width, detailControl.normalizedRadius * parent.width - width / 2))
                    y: Math.max(0, Math.min(parent.height - height, (1 - detailControl.normalizedSharpen) * parent.height - height / 2))
                    width: 18
                    height: 18
                    radius: 4
                    rotation: 45
                    color: detailControl.sharpening > 0.5 ? detailControl.accent : theme.alpha(theme.polar, 0.92)
                    border.width: 2
                    border.color: theme.alpha(theme.ink950, 0.76)
                }
                Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 7
                    anchors.bottomMargin: 5
                    text: "RADIUS"
                    color: theme.alpha(theme.textStrong, 0.70)
                    font.family: theme.fontUi
                    font.pixelSize: 8
                    font.weight: Font.Bold
                }
                Label {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 7
                    anchors.topMargin: 5
                    text: "AMOUNT"
                    color: theme.alpha(theme.textStrong, 0.70)
                    font.family: theme.fontUi
                    font.pixelSize: 8
                    font.weight: Font.Bold
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.CrossCursor
                    onPressed: function(mouse) {
                        detailControl.interactionStarted()
                        detailControl.editFocus(mouse.x, mouse.y)
                    }
                    onPositionChanged: function(mouse) {
                        if (pressed)
                            detailControl.editFocus(mouse.x, mouse.y)
                    }
                    onReleased: {
                        detailControl.flushValueEdits()
                        detailControl.interactionFinished()
                    }
                    onCanceled: {
                        detailControl.flushValueEdits()
                        detailControl.interactionFinished()
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 74
                Layout.fillHeight: true
                spacing: 5

                Repeater {
                    model: [
                        { key: "sharpenMasking", label: "Edge Limit", value: detailControl.masking },
                        { key: "luminanceNr", label: "Luma", value: detailControl.luminanceNr },
                        { key: "colorNr", label: "Color", value: detailControl.colorNr }
                    ]

                    Rectangle {
                        id: detailRail
                        required property var modelData
                        readonly property real railValue: Number(modelData.value || 0)
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 26
                        radius: 3
                        color: theme.alpha(theme.ink950, 0.34)
                        border.width: 1
                        border.color: theme.alpha(theme.polar, 0.10)

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: Math.max(1, parent.height * detailRail.railValue / 100)
                            color: detailControl.accent
                            opacity: detailRail.railValue > 0.5 ? 0.58 : 0
                        }
                        Label {
                            anchors.centerIn: parent
                            text: String(detailRail.modelData.label || "")
                            color: detailRail.railValue > 0.5 ? theme.ink950 : "#C8D2DC"
                            font.family: theme.fontUi
                            font.pixelSize: 8
                            font.weight: Font.Black
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.SizeVerCursor
                            onPressed: function(mouse) {
                                detailControl.interactionStarted()
                                detailControl.editRail(String(detailRail.modelData.key || ""), mouse.y, height)
                            }
                            onPositionChanged: function(mouse) {
                                if (pressed)
                                    detailControl.editRail(String(detailRail.modelData.key || ""), mouse.y, height)
                            }
                            onReleased: {
                                detailControl.flushValueEdits()
                                detailControl.interactionFinished()
                            }
                            onCanceled: {
                                detailControl.flushValueEdits()
                                detailControl.interactionFinished()
                            }
                            onDoubleClicked: {
                                detailControl.flushValueEdits()
                                detailControl.valueEdited(String(detailRail.modelData.key || ""), 0)
                            }
                        }
                    }
                }
            }
        }
    }
}
