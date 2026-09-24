import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: control

    property string title: ""
    property string valueText: ""
    property real hueValue: 0
    property real saturationValue: 0
    property real luminanceValue: 0
    property color accent: theme.accentWarm400
    property color channelColor: theme.accentWarm400
    property int editCommitIntervalMs: 16
    property real pendingHueValue: 0
    property real pendingLuminanceValue: 0
    property real pendingSaturationValue: 0
    property bool hasPendingHueLuminanceEdit: false
    property bool hasPendingSaturationEdit: false
    readonly property real normalizedHue: clamp((hueValue + 100) / 200, 0, 1)
    readonly property real normalizedLuma: clamp((100 - luminanceValue) / 200, 0, 1)

    signal hueLuminanceEdited(real hueValue, real luminanceValue)
    signal saturationEdited(real saturationValue)
    signal interactionStarted()
    signal interactionFinished()

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.preferredHeight: 132
    radius: 4
    color: theme.alpha(theme.glassBase, 0.42)
    border.width: 0
    clip: true

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function hueLumaFromPadPoint(mouseX, mouseY) {
        return {
            hue: clamp(mouseX, 0, padField.width) / Math.max(1, padField.width) * 200 - 100,
            luma: 100 - clamp(mouseY, 0, padField.height) / Math.max(1, padField.height) * 200
        }
    }

    function saturationFromRailY(mouseY) {
        return clamp(100 - mouseY / Math.max(1, saturationRail.height) * 200, -100, 100)
    }

    function queueHueLuminanceEdited(nextHue, nextLuminance) {
        if (!editCommitTimer.running && !hasPendingHueLuminanceEdit && !hasPendingSaturationEdit) {
            hueLuminanceEdited(nextHue, nextLuminance)
            editCommitTimer.start()
            return
        }
        pendingHueValue = nextHue
        pendingLuminanceValue = nextLuminance
        hasPendingHueLuminanceEdit = true
        if (!editCommitTimer.running)
            editCommitTimer.start()
    }

    function queueSaturationEdited(nextSaturation) {
        if (!editCommitTimer.running && !hasPendingHueLuminanceEdit && !hasPendingSaturationEdit) {
            saturationEdited(nextSaturation)
            editCommitTimer.start()
            return
        }
        pendingSaturationValue = nextSaturation
        hasPendingSaturationEdit = true
        if (!editCommitTimer.running)
            editCommitTimer.start()
    }

    function flushQueuedEdits() {
        const emitHueLuma = hasPendingHueLuminanceEdit
        const nextHue = pendingHueValue
        const nextLuminance = pendingLuminanceValue
        const emitSaturation = hasPendingSaturationEdit
        const nextSaturation = pendingSaturationValue
        hasPendingHueLuminanceEdit = false
        hasPendingSaturationEdit = false
        editCommitTimer.stop()
        if (emitHueLuma)
            hueLuminanceEdited(nextHue, nextLuminance)
        if (emitSaturation)
            saturationEdited(nextSaturation)
    }

    Timer {
        id: editCommitTimer
        interval: control.editCommitIntervalMs
        repeat: false
        onTriggered: control.flushQueuedEdits()
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
                text: control.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.Black
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0
                elide: Text.ElideNone
            }
            Label {
                text: control.valueText
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
                id: padField
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 78

                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: theme.alpha(control.channelColor, 0.26) }
                        GradientStop { position: 0.5; color: theme.alpha(theme.polar, 0.10) }
                        GradientStop { position: 1.0; color: theme.alpha(control.channelColor, 0.68) }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: theme.alpha(theme.polar, 0.35) }
                        GradientStop { position: 0.5; color: theme.alpha(theme.ink950, 0.24) }
                        GradientStop { position: 1.0; color: theme.alpha(theme.ink950, 0.76) }
                    }
                    border.width: 1
                    border.color: theme.alpha(theme.polar, 0.14)
                }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; height: 1; color: theme.alpha(theme.polar, 0.24) }
                Rectangle { anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: 1; color: theme.alpha(theme.polar, 0.24) }
                Rectangle {
                    x: Math.max(0, Math.min(parent.width - width, control.normalizedHue * parent.width - width / 2))
                    y: Math.max(0, Math.min(parent.height - height, control.normalizedLuma * parent.height - height / 2))
                    width: 18; height: 18; radius: 4; rotation: 45
                    color: theme.alpha(theme.polar, 0.94)
                    border.width: 2
                    border.color: theme.alpha(control.accent, 0.96)
                }
                Label {
                    anchors.left: parent.left; anchors.bottom: parent.bottom
                    anchors.leftMargin: 7; anchors.bottomMargin: 5
                    text: "HUE"; color: theme.alpha(theme.textStrong, 0.70)
                    font.family: theme.fontUi; font.pixelSize: 8; font.weight: Font.Bold
                }
                Label {
                    anchors.right: parent.right; anchors.top: parent.top
                    anchors.rightMargin: 7; anchors.topMargin: 5
                    text: "LUMA"; color: theme.alpha(theme.textStrong, 0.70)
                    font.family: theme.fontUi; font.pixelSize: 8; font.weight: Font.Bold
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.CrossCursor
                    onPressed: function(mouse) {
                        control.interactionStarted()
                        const next = control.hueLumaFromPadPoint(mouse.x, mouse.y)
                        control.queueHueLuminanceEdited(next.hue, next.luma)
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const next = control.hueLumaFromPadPoint(mouse.x, mouse.y)
                        control.queueHueLuminanceEdited(next.hue, next.luma)
                    }
                    onReleased: { control.flushQueuedEdits(); control.interactionFinished() }
                    onCanceled: { control.flushQueuedEdits(); control.interactionFinished() }
                    onDoubleClicked: control.hueLuminanceEdited(0, 0)
                }
            }

            Item {
                id: saturationRail
                Layout.preferredWidth: 32
                Layout.fillHeight: true
                Layout.minimumHeight: 78
                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: control.channelColor }
                        GradientStop { position: 1.0; color: theme.alpha(theme.polar, 0.18) }
                    }
                    border.width: 1
                    border.color: theme.alpha(theme.polar, 0.14)
                }
                Rectangle {
                    x: 4
                    y: Math.max(0, Math.min(parent.height - height, (100 - control.saturationValue) / 200 * parent.height - height / 2))
                    width: parent.width - 8; height: 4; radius: 2; color: control.accent
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 5
                    text: "SAT"; color: theme.alpha(theme.ink950, 0.84)
                    font.family: theme.fontUi; font.pixelSize: 8; font.weight: Font.Bold
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    onPressed: function(mouse) { control.interactionStarted(); control.queueSaturationEdited(control.saturationFromRailY(mouse.y)) }
                    onPositionChanged: function(mouse) { if (pressed) control.queueSaturationEdited(control.saturationFromRailY(mouse.y)) }
                    onReleased: { control.flushQueuedEdits(); control.interactionFinished() }
                    onCanceled: { control.flushQueuedEdits(); control.interactionFinished() }
                    onDoubleClicked: control.saturationEdited(0)
                }
            }
        }
    }
}
