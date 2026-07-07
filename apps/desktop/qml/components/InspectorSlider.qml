import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Item {
    id: root

    property string label: ""
    property string valueText: ""
    property color accent: theme.accentWarm400
    property var trackColors: []
    property bool valueInputEnabled: true
    property string valueInputMode: "raw"
    property string disabledReason: ""
    property bool showFullLabelTooltip: true
    property int labelColumnWidth: theme.labelMinWidth
    property int valueColumnWidth: theme.valueColumnWidth
    property int resetColumnWidth: 28
    property bool showResetButton: true
    property real defaultValue: from <= 0 && to >= 0 ? 0 : from
    property real modifiedEpsilon: stepSize > 0 ? Math.max(stepSize / 2, 0.0001) : 0.0001
    property int controlHeight: 44
    readonly property real sliderScale: Math.max(1.0, controlHeight / 34)
    readonly property int trackHeight: 3
    readonly property int thumbRestSize: 14
    readonly property int thumbPressedSize: 18
    readonly property int thumbReservedSize: Math.max(thumbRestSize, thumbPressedSize)
    readonly property real thumbVisualScale: thumbReservedSize > 0
                                       ? (control.pressed ? 1.0 : thumbRestSize / thumbReservedSize)
                                       : 1.0
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
    readonly property string resolvedTooltipText: !root.enabled && root.disabledReason.length > 0
                                                 ? root.label + " - " + root.disabledReason
                                                 : root.label
    readonly property string sliderContract: "inspector-slider:v2|label-column:" +
                                             String(labelColumnWidth) +
                                             "|value-column:" +
                                             String(valueColumnWidth) +
                                             "|reset-column:" +
                                             String(resetColumnWidth) +
                                             "|row-height:" +
                                             String(controlHeight) +
                                             "|track:" +
                                             String(trackHeight) +
                                             "|thumb:" +
                                             String(thumbRestSize) +
                                             "-" +
                                             String(thumbPressedSize) +
                                             "|reserved-thumb:" +
                                             String(thumbReservedSize) +
                                             "|typed-entry:" +
                                             String(valueInputEnabled) +
                                             "|modified-marker:true|center-marker:" +
                                             String(hasCenterMarker) +
                                             "|default-marker:true|fixed-reset:true|stable-value:true|fixed-label:true|disabled-reason:true|no-elide:true|double-click-reset:true|no-handle-layout-shift:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|value-fit:true|tooltip-full:true|accessibility-full:true"
    readonly property string valueGeometryContract: "slider-value-geometry:v2|label-column-fixed:true|value-column-fixed:true|reset-column-fixed:true|row-height:44|track-height:3|thumb:14-18|reserved-thumb:18|handle-layout-shift:false"
    readonly property string resetActionContract: "slider-reset-action:v1|icon-only:true|tooltip-full:true|accessible-label:true|disabled-reason:true|active-when-modified:true|fixed-column:" +
                                                  String(root.resetColumnWidth)
    signal interactionStarted()
    signal interactionFinished()
    signal valueDragged(real value)
    signal resetRequested()

    Theme { id: theme }

    implicitHeight: controlHeight
    Accessible.name: root.label
    Accessible.description: !root.enabled && root.disabledReason.length > 0 ? root.disabledReason : root.valueText

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
            id: labelText
            Layout.preferredWidth: root.labelColumnWidth
            Layout.minimumWidth: root.labelColumnWidth
            Layout.maximumWidth: root.labelColumnWidth
            Layout.alignment: Qt.AlignVCenter
            text: root.label
            color: root.enabled
                   ? (root.modified ? root.accent : (control.pressed ? theme.textStrong : theme.textMain))
                   : theme.textFaint
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            maximumLineCount: 1
            font.family: theme.fontUi
            font.pixelSize: 11
            font.weight: root.modified || control.pressed ? Font.Black : Font.Bold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0
            ToolTip.visible: root.showFullLabelTooltip &&
                             (labelText.contentWidth > labelText.width + 1 ||
                              (!root.enabled && root.disabledReason.length > 0)) &&
                             labelHover.hovered
            ToolTip.text: root.resolvedTooltipText

            HoverHandler {
                id: labelHover
            }

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
                    height: Math.max(1, Math.round(root.trackHeight * root.sliderScale))

                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: root.hasTrackGradient ? "transparent" : theme.alpha(theme.polar, 0.18)
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
                        radius: height / 2
                        color: Array.isArray(root.trackColors) && root.trackColors.length > 1
                               ? theme.alpha(theme.ink950, 0.16)
                               : theme.alpha(theme.ink950, 0.24)
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
                        height: Math.max(1, Math.round(root.trackHeight * root.sliderScale))
                        anchors.verticalCenter: parent.verticalCenter
                        radius: height / 2
                        color: root.accent
                        opacity: 0.82
                    }
                }

                handle: Rectangle {
                    x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
                    y: control.topPadding + control.availableHeight / 2 - height / 2
                    width: Math.round(root.thumbReservedSize * root.sliderScale)
                    height: width
                    radius: 4
                    rotation: 45
                    color: "transparent"

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * root.thumbVisualScale
                        height: width
                        radius: 4
                        color: control.pressed ? theme.polar : (root.modified ? root.accent : theme.alpha(theme.polar, 0.90))
                        border.width: 2
                        border.color: control.pressed ? theme.alpha(root.accent, 0.95) : theme.alpha(theme.ink950, 0.76)

                        Behavior on width {
                            NumberAnimation { duration: theme.durationFast }
                        }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: control.pressed ? 4 : 3
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
            Layout.preferredWidth: root.valueColumnWidth
            Layout.minimumWidth: root.valueColumnWidth
            Layout.maximumWidth: root.valueColumnWidth
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignRight
            enabled: root.enabled && root.valueInputEnabled
            text: root.valueText
            color: root.enabled ? (root.modified ? root.accent : (root.pressed ? theme.textStrong : "#C8D2DC")) : theme.textFaint
            font.family: theme.fontMono
            font.pixelSize: 11
            font.weight: Font.Bold
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

        Components.ToolButton {
            objectName: root.objectName.length > 0 ? root.objectName + "ResetButton" : ""
            Layout.preferredWidth: root.resetColumnWidth
            Layout.minimumWidth: root.resetColumnWidth
            Layout.maximumWidth: root.resetColumnWidth
            Layout.preferredHeight: Math.min(root.controlHeight - 8, 30)
            Layout.alignment: Qt.AlignVCenter
            visible: root.showResetButton
            iconName: "reset"
            compact: true
            showLabel: false
            label: root.label.length > 0 ? "Reset " + root.label : "Reset value"
            active: root.modified
            enabled: root.enabled && root.modified
            commandEnabled: root.enabled && root.modified
            disabledReason: root.modified ? "" : "Value is already at its default."
            onClicked: if (root.modified) root.resetRequested()
        }
    }
}
