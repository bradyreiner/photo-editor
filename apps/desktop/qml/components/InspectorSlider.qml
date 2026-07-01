import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string label: ""
    property string valueText: ""
    property color accent: theme.accentWarm400
    property var trackColors: []
    property bool valueInputEnabled: true
    property string valueInputMode: "raw"
    property real defaultValue: from <= 0 && to >= 0 ? 0 : from
    property real modifiedEpsilon: stepSize > 0 ? Math.max(stepSize / 2, 0.0001) : 0.0001
    property int controlHeight: 34
    readonly property real sliderScale: Math.max(1.0, controlHeight / 34)
    property alias from: control.from
    property alias to: control.to
    property alias stepSize: control.stepSize
    property alias value: control.value
    readonly property alias pressed: control.pressed
    readonly property real normalizedValue: (control.to - control.from) !== 0 ? (control.value - control.from) / (control.to - control.from) : 0
    readonly property real defaultPosition: (control.to - control.from) !== 0 ? clamp((defaultValue - control.from) / (control.to - control.from), 0, 1) : 0
    readonly property bool hasCenterMarker: control.from < 0 && control.to > 0
    readonly property real centerMarkerPosition: hasCenterMarker ? (0 - control.from) / (control.to - control.from) : 0
    readonly property real fillStartPosition: Math.min(defaultPosition, control.visualPosition)
    readonly property real fillEndPosition: Math.max(defaultPosition, control.visualPosition)
    readonly property bool modified: Math.abs(control.value - defaultValue) > modifiedEpsilon
    readonly property int trackColorCount: Array.isArray(trackColors) ? trackColors.length : 0
    readonly property bool hasTrackGradient: trackColorCount > 1
    signal interactionStarted()
    signal interactionFinished()
    signal valueDragged(real value)
    signal resetRequested()

    Theme { id: theme }

    implicitHeight: controlHeight

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function kelvinToControlValue(kelvin) {
        const boundedKelvin = clamp(kelvin, 2000, 12000)
        if (boundedKelvin >= 5500)
            return ((boundedKelvin - 5500) / (12000 - 5500)) * 100
        return -((5500 - boundedKelvin) / (5500 - 2000)) * 100
    }

    function parsedInputValue(text) {
        const numeric = Number(String(text).replace(/[^0-9+\-.]/g, ""))
        if (!Number.isFinite(numeric))
            return control.value
        if (root.valueInputMode === "kelvin")
            return kelvinToControlValue(numeric)
        if (root.valueInputMode === "degreesNormalized")
            return numeric / 360
        if (root.valueInputMode === "percentNormalized")
            return numeric / 100
        return numeric
    }

    function commitValueText() {
        if (!root.valueInputEnabled)
            return
        const nextValue = clamp(parsedInputValue(valueField.text), control.from, control.to)
        valueField.text = root.valueText
        root.valueDragged(nextValue)
    }

    function testCommitInputText(text) {
        valueField.text = String(text)
        commitValueText()
        return control.value
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onDoubleTapped: root.resetRequested()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space6
        anchors.rightMargin: theme.space6
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        spacing: theme.space8

        Label {
            Layout.preferredWidth: 102
            Layout.alignment: Qt.AlignVCenter
            text: root.label
            color: root.enabled
                   ? (root.modified ? root.accent : (control.pressed ? theme.textMain : theme.textMuted))
                   : theme.textFaint
            elide: Text.ElideRight
            font.family: theme.fontUi
            font.pixelSize: 10
            font.weight: root.modified || control.pressed ? Font.DemiBold : Font.Medium
            font.letterSpacing: 0

            Rectangle {
                objectName: root.objectName.length > 0 ? root.objectName + "ModifiedMarker" : ""
                visible: root.modified
                width: 4
                height: 4
                radius: width / 2
                anchors.left: parent.left
                anchors.leftMargin: -7
                anchors.verticalCenter: parent.verticalCenter
                color: root.accent
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            implicitHeight: Math.max(20, root.controlHeight - 14)

            Slider {
                id: control
                anchors.fill: parent
                anchors.leftMargin: 2
                anchors.rightMargin: 2
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0

                background: Item {
                    x: control.leftPadding
                    y: control.topPadding + control.availableHeight / 2 - height / 2
                    width: control.availableWidth
                    height: Math.round(2 * root.sliderScale)

                    Rectangle {
                        anchors.fill: parent
                        radius: 0
                        color: root.hasTrackGradient ? "transparent" : theme.alpha(theme.polar, 0.19)
                    }

                    Row {
                        id: gradientSegments
                        anchors.fill: parent
                        visible: root.hasTrackGradient
                        spacing: 0
                        clip: true

                        Repeater {
                            model: Math.max(0, root.trackColorCount - 1)

                            Rectangle {
                                width: gradientSegments.width / Math.max(1, root.trackColorCount - 1)
                                height: gradientSegments.height
                                radius: 0
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: root.trackColors[index] }
                                    GradientStop { position: 1.0; color: root.trackColors[index + 1] }
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: 0
                        color: Array.isArray(root.trackColors) && root.trackColors.length > 1
                               ? theme.alpha(theme.ink950, 0.20)
                               : theme.alpha(theme.ink950, 0.28)
                    }

                    Rectangle {
                        visible: root.hasCenterMarker
                        x: Math.max(0, Math.min(parent.width - width, root.centerMarkerPosition * parent.width - width / 2))
                        width: 1
                        height: Math.round(7 * root.sliderScale)
                        radius: 0
                        anchors.verticalCenter: parent.verticalCenter
                        color: theme.alpha(theme.polar, 0.34)
                    }

                    Rectangle {
                        visible: root.modified
                        x: root.fillStartPosition * parent.width
                        width: Math.max(1, (root.fillEndPosition - root.fillStartPosition) * parent.width)
                        height: Math.round(2 * root.sliderScale)
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 0
                        color: root.accent
                        opacity: 0.82
                    }
                }

                handle: Rectangle {
                    x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
                    y: control.topPadding + control.availableHeight / 2 - height / 2
                    width: Math.round((control.pressed ? 13 : 11) * root.sliderScale)
                    height: width
                    radius: width / 2
                    color: control.pressed ? theme.polar : (root.modified ? root.accent : theme.alpha(theme.polar, 0.86))
                    border.width: 1
                    border.color: control.pressed ? theme.alpha(root.accent, 0.9) : theme.alpha(theme.polar, 0.42)

                    Behavior on width {
                        NumberAnimation { duration: theme.durationFast }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: control.pressed ? 3 : 2
                        height: width
                        radius: width / 2
                        color: control.pressed ? root.accent : theme.alpha(theme.ink950, 0.86)
                    }
                }

                onMoved: root.valueDragged(control.value)
                onPressedChanged: {
                    if (pressed)
                        root.interactionStarted()
                    else
                        root.interactionFinished()
                }
            }
        }

        TextField {
            id: valueField
            objectName: root.objectName.length > 0 ? root.objectName + "ValueField" : ""
            Layout.preferredWidth: 58
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignRight
            enabled: root.enabled && root.valueInputEnabled
            text: root.valueText
            color: root.enabled ? (root.modified ? root.accent : (root.pressed ? theme.textStrong : "#B5C0CA")) : theme.textFaint
            font.family: theme.fontMono
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0
            selectByMouse: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            background: Rectangle {
                color: valueField.activeFocus ? theme.alpha(root.accent, 0.08) : "transparent"
                border.width: valueField.activeFocus || root.modified ? 1 : 0
                border.color: theme.alpha(root.accent, valueField.activeFocus ? 0.65 : 0.32)
                radius: 2
            }
            onAccepted: root.commitValueText()
            onActiveFocusChanged: {
                if (!activeFocus)
                    root.commitValueText()
            }
        }
    }
}
