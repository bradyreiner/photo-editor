import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: lensControl

    property real distortion: 0
    property real vignette: 0
    property real chromatic: 0
    property color accent: theme.accentWarm400
    property int editCommitIntervalMs: 16
    property var pendingValues: ({})
    property var pendingKeys: []
    readonly property real normalizedDistortion: clamp((distortion + 100) / 200, 0, 1)
    readonly property real normalizedVignette: clamp((100 - vignette) / 200, 0, 1)

    signal valueEdited(string key, real value)
    signal interactionStarted()
    signal interactionFinished()

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.preferredHeight: 128
    radius: 4
    color: theme.alpha(theme.glassBase, 0.42)
    border.width: 0
    clip: true

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function signedPercent(value) {
        return (value > 0 ? "+" : "") + Math.round(value) + "%"
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

    function editGeometry(mouseX, mouseY) {
        queueValueEdited("lensDistortion", clamp(mouseX / Math.max(1, geometryPad.width) * 200 - 100, -100, 100))
        queueValueEdited("lensVignette", clamp(100 - mouseY / Math.max(1, geometryPad.height) * 200, -100, 100))
    }

    function editChromatic(mouseY) {
        queueValueEdited("chromaticAberration", clamp(100 - mouseY / Math.max(1, chromaticRail.height) * 100, 0, 100))
    }

    Timer {
        id: editCommitTimer
        interval: lensControl.editCommitIntervalMs
        repeat: false
        onTriggered: lensControl.flushValueEdits()
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
                text: "Optical Correction"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.Black
                font.capitalization: Font.AllUppercase
            }
            Label {
                text: lensControl.signedPercent(lensControl.distortion) + " / " +
                      lensControl.signedPercent(lensControl.vignette) + " / " +
                      Math.round(lensControl.chromatic)
                color: "#C8D2DC"
                font.family: theme.fontMono
                font.pixelSize: 9
                font.weight: Font.Bold
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Item {
                id: geometryPad
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 72

                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    color: theme.alpha(theme.ink950, 0.38)
                    border.width: 1
                    border.color: theme.alpha(theme.polar, 0.14)
                }
                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        anchors.centerIn: parent
                        width: parent.width * (0.28 + index * 0.18)
                        height: parent.height * (0.34 + index * 0.14)
                        radius: Math.min(width, height) / 2
                        color: "transparent"
                        border.width: 1
                        border.color: theme.alpha(theme.polar, 0.09 + index * 0.03)
                    }
                }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; height: 1; color: theme.alpha(theme.polar, 0.20) }
                Rectangle { anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: 1; color: theme.alpha(theme.polar, 0.20) }
                Rectangle {
                    x: Math.max(0, Math.min(parent.width - width, lensControl.normalizedDistortion * parent.width - width / 2))
                    y: Math.max(0, Math.min(parent.height - height, lensControl.normalizedVignette * parent.height - height / 2))
                    width: 18; height: 18; radius: 4; rotation: 45
                    color: Math.abs(lensControl.distortion) > 0.5 || Math.abs(lensControl.vignette) > 0.5 ? lensControl.accent : theme.alpha(theme.polar, 0.92)
                    border.width: 2
                    border.color: theme.alpha(theme.ink950, 0.76)
                }
                Label { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 7; text: "DISTORT"; color: theme.alpha(theme.textStrong, 0.70); font.family: theme.fontUi; font.pixelSize: 8; font.weight: Font.Bold }
                Label { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 7; text: "VIGNETTE"; color: theme.alpha(theme.textStrong, 0.70); font.family: theme.fontUi; font.pixelSize: 8; font.weight: Font.Bold }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.CrossCursor
                    onPressed: function(mouse) { lensControl.interactionStarted(); lensControl.editGeometry(mouse.x, mouse.y) }
                    onPositionChanged: function(mouse) { if (pressed) lensControl.editGeometry(mouse.x, mouse.y) }
                    onReleased: { lensControl.flushValueEdits(); lensControl.interactionFinished() }
                    onCanceled: { lensControl.flushValueEdits(); lensControl.interactionFinished() }
                    onDoubleClicked: {
                        lensControl.flushValueEdits()
                        lensControl.valueEdited("lensDistortion", 0)
                        lensControl.valueEdited("lensVignette", 0)
                    }
                }
            }

            Item {
                id: chromaticRail
                Layout.preferredWidth: 34
                Layout.fillHeight: true
                Layout.minimumHeight: 72
                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: "#F25A5A" }
                        GradientStop { position: 0.5; color: lensControl.accent }
                        GradientStop { position: 1.0; color: "#4D91F0" }
                    }
                    border.width: 1
                    border.color: theme.alpha(theme.polar, 0.14)
                }
                Rectangle {
                    x: 4
                    y: Math.max(0, Math.min(parent.height - height, (1 - lensControl.chromatic / 100) * parent.height - height / 2))
                    width: parent.width - 8; height: 4; radius: 2; color: theme.polar
                }
                Label { anchors.centerIn: parent; rotation: -90; text: "CA"; color: theme.ink950; font.family: theme.fontUi; font.pixelSize: 8; font.weight: Font.Black }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    onPressed: function(mouse) { lensControl.interactionStarted(); lensControl.editChromatic(mouse.y) }
                    onPositionChanged: function(mouse) { if (pressed) lensControl.editChromatic(mouse.y) }
                    onReleased: { lensControl.flushValueEdits(); lensControl.interactionFinished() }
                    onCanceled: { lensControl.flushValueEdits(); lensControl.interactionFinished() }
                    onDoubleClicked: { lensControl.flushValueEdits(); lensControl.valueEdited("chromaticAberration", 0) }
                }
            }
        }
    }
}
