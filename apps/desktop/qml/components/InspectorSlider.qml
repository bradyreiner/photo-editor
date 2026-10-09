import QtQuick
import QtQuick.Controls.Basic
import "." as Components

Item {
    id: root

    property string label: ""
    readonly property point agentContactPoint: Qt.point(control.x + control.handle.x + control.handle.width / 2,
                                                       control.y + control.handle.y + control.handle.height / 2)
    readonly property rect agentSurfaceRect: Qt.rect(control.x + control.handle.width / 2,
        agentContactPoint.y, control.width - control.handle.width, 0)
    // A symmetric quadratic track gives exposure more room around zero while
    // keeping the value, keyboard increment and saved recipe in actual EV.
    property bool precisionResponse: false
    readonly property real displayedPosition: precisionResponse
                                              ? (control.mirrored ? 1 - positionForValue(control.value) : positionForValue(control.value))
                                              : control.visualPosition
    property string valueText: ""
    property color accent: theme.accentWarm400
    property var trackColors: []
    property bool valueInputEnabled: true
    property bool valueInputDirty: false
    property string valueInputMode: "raw"
    property string disabledReason: ""
    property bool showFullLabelTooltip: true
    property int labelColumnWidth: theme.labelMinWidth
    property int valueColumnWidth: Math.max(theme.valueColumnWidth, 68)
    property int resetColumnWidth: 28
    property bool showResetButton: true
    property bool throttleDraggedValues: true
    property int dragCommitIntervalMs: 16
    property real pendingDraggedValue: 0
    property bool hasPendingDraggedValue: false
    property real defaultValue: from <= 0 && to >= 0 ? 0 : from
    property real modifiedEpsilon: stepSize > 0 ? Math.max(stepSize / 2, 0.0001) : 0.0001
    property int controlHeight: theme.scaled(theme.densityComfortable ? 62 : theme.densityCompact ? 50 : 56)
    property bool forceCompact: false
    readonly property bool wideRailLayout: !forceCompact && (controlHeight >= theme.scaled(52)
                                            || width < theme.scaled(280))
    readonly property int effectiveResetWidth: showResetButton ? resetColumnWidth : 0
    readonly property real compactLabelWidth: Math.max(
                                                  theme.scaled(32),
                                                  Math.min(Math.max(theme.scaled(48), labelColumnWidth),
                                                           width - valueColumnWidth - effectiveResetWidth
                                                           - theme.space12 - theme.space24))
    readonly property real compactTrackX: theme.space6 + compactLabelWidth + theme.space8
    readonly property real compactValueX: width - theme.space6 - effectiveResetWidth
                                          - (effectiveResetWidth > 0 ? theme.space8 : 0)
                                          - valueColumnWidth
    readonly property real resolvedSliderHeight: wideRailLayout
                                                 ? Math.max(controlHeight, theme.controlHeightDense + theme.scaled(24) + theme.space6)
                                                 : Math.max(controlHeight, theme.controlHeightDense)
    readonly property real sliderScale: theme.interfaceScale
    readonly property int trackHeight: 3
    readonly property int thumbRestSize: 12
    readonly property int thumbPressedSize: 16
    readonly property int thumbReservedSize: Math.max(thumbRestSize, thumbPressedSize)
    readonly property real thumbVisualScale: thumbReservedSize > 0
                                       ? (root.pressed ? 1.0 : thumbRestSize / thumbReservedSize)
                                       : 1.0
    property alias from: control.from
    property alias to: control.to
    property alias stepSize: control.stepSize
    property alias value: control.value
    readonly property bool pressed: control.pressed || precisionPointer.gestureActive
    readonly property real normalizedValue: (control.to - control.from) !== 0 ? (control.value - control.from) / (control.to - control.from) : 0
    readonly property real defaultPosition: (control.to - control.from) !== 0 ? clamp((defaultValue - control.from) / (control.to - control.from), 0, 1) : 0
    readonly property bool hasCenterMarker: control.from < 0 && control.to > 0
    readonly property real centerMarkerPosition: hasCenterMarker ? (0 - control.from) / (control.to - control.from) : 0
    readonly property real fillStartPosition: Math.min(defaultPosition, root.displayedPosition)
    readonly property real fillEndPosition: Math.max(defaultPosition, root.displayedPosition)
    readonly property bool modified: Math.abs(control.value - defaultValue) > modifiedEpsilon
    readonly property int trackColorCount: Array.isArray(trackColors) ? trackColors.length : 0
    readonly property bool hasTrackGradient: trackColorCount > 1
    readonly property string resolvedTooltipText: !root.enabled && root.disabledReason.length > 0
                                                 ? root.label + " - " + root.disabledReason
                                                 : root.label + (root.precisionResponse ? " · Shift: fine adjustment · Esc: cancel" : "")
    readonly property string sliderContract: "inspector-slider:v4|layout:adaptive-compact-row-or-label-value-header-wide-rail|label-column:fluid|value-column:" +
                                             String(valueColumnWidth) +
                                             "|reset-column:" +
                                             String(resetColumnWidth) +
                                             "|row-height:" +
                                             String(resolvedSliderHeight) +
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
                                             "|modified-marker:false|wide-rail:" + String(wideRailLayout) + "|compact-row:" +
                                             String(!wideRailLayout) + "|center-marker:" +
                                             String(hasCenterMarker) +
                                             "|default-marker:true|fixed-reset:true|stable-value:true|label-layout:fluid|disabled-reason:true|no-elide:true|double-click-reset:true|no-handle-layout-shift:true|drag-commit-throttle:" +
                                             String(throttleDraggedValues) +
                                             "|drag-commit-interval-ms:" +
                                             String(dragCommitIntervalMs)
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|value-fit:true|tooltip-full:true|accessibility-full:true"
    readonly property string valueGeometryContract: "slider-value-geometry:v5|layout:adaptive-compact-row-or-label-value-header-wide-rail|label-column-fixed:false|value-column-fixed:true|reset-column-fixed:true|wide-row-height:density-fit|compact-row-height:configured|track-hit-height:24|track-height:3|thumb:12-16|reserved-thumb:16|handle-layout-shift:false"
    readonly property string sliderVisualContract: "inspector-slider-visual:v1|thumb-fill:neutral-pearl|rest-outline:1px|focus-outline:2px|reserved-handle-size:true|motion:reduced-motion-aware"
    readonly property string resetActionContract: "slider-reset-action:v1|icon-only:true|tooltip-full:true|accessible-label:true|disabled-reason:true|active-when-modified:true|fixed-column:" +
                                                  String(root.resetColumnWidth)
    signal interactionStarted()
    signal interactionFinished()
    signal valueDragged(real value)
    signal resetRequested()

    Theme { id: theme }

    implicitHeight: resolvedSliderHeight
    Accessible.name: root.label
    Accessible.description: !root.enabled && root.disabledReason.length > 0 ? root.disabledReason : root.valueText

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    function positionForValue(value) {
        if (precisionResponse && control.from < 0 && control.to > 0) {
            const normalized = value < 0 ? value / -control.from : value / control.to
            return 0.5 + Math.sign(normalized) * Math.sqrt(Math.abs(normalized)) * 0.5
        }
        return control.to !== control.from ? (value - control.from) / (control.to - control.from) : 0
    }

    function valueForPosition(position) {
        const bounded = clamp(position, 0, 1)
        if (precisionResponse && control.from < 0 && control.to > 0) {
            const signed = bounded * 2 - 1
            return signed * Math.abs(signed) * (signed < 0 ? -control.from : control.to)
        }
        return control.from + bounded * (control.to - control.from)
    }

    function kelvinToControlValue(kelvin) {
        const boundedKelvin = clamp(kelvin, 2000, 12000)
        if (boundedKelvin >= 5500)
            return ((boundedKelvin - 5500) / (12000 - 5500)) * 100
        return -((5500 - boundedKelvin) / (5500 - 2000)) * 100
    }

    function parsedInputValue(text) {
        // The displayed value may include a unit (for example "0.3 EV"),
        // but stripping arbitrary characters turns an empty or malformed
        // edit into Number("") === 0. Accept a complete numeric token and an
        // optional unit suffix only.
        const match = String(text).trim().match(/^([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*(?:[A-Za-z%°]+)?$/)
        if (!match)
            return NaN
        const numeric = Number(match[1])
        if (!Number.isFinite(numeric))
            return NaN
        if (root.valueInputMode === "kelvin")
            return kelvinToControlValue(numeric)
        if (root.valueInputMode === "degreesNormalized")
            return numeric / 360
        if (root.valueInputMode === "percentNormalized")
            return numeric / 100
        if (root.valueInputMode === "gamma")
            return numeric > 0 ? -Math.log(numeric) / Math.LN2 * 100 / 1.45 : NaN
        return numeric
    }

    function commitValueText() {
        if (!root.valueInputEnabled || !root.valueInputDirty)
            return false
        const parsedValue = parsedInputValue(valueField.text)
        root.valueInputDirty = false
        if (!Number.isFinite(parsedValue)) {
            valueField.text = Qt.binding(function() { return root.valueText })
            return false
        }
        const nextValue = clamp(parsedValue, control.from, control.to)
        root.valueDragged(nextValue)
        // Typing replaces the text binding. Restore it after committing so a
        // later focus change cannot submit the previous displayed value.
        valueField.text = Qt.binding(function() { return root.valueText })
        return true
    }

    function queueDraggedValue(value) {
        if (!root.throttleDraggedValues) {
            root.valueDragged(value)
            return
        }
        if (!dragCommitTimer.running && !root.hasPendingDraggedValue) {
            root.valueDragged(value)
            dragCommitTimer.start()
            return
        }
        root.pendingDraggedValue = value
        root.hasPendingDraggedValue = true
        if (!dragCommitTimer.running)
            dragCommitTimer.start()
    }

    function flushQueuedDraggedValue() {
        if (!root.hasPendingDraggedValue)
            return
        const value = root.pendingDraggedValue
        root.hasPendingDraggedValue = false
        dragCommitTimer.stop()
        root.valueDragged(value)
    }

    function testCommitInputText(text) {
        valueField.text = String(text)
        root.valueInputDirty = true
        commitValueText()
        return control.value
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onDoubleTapped: root.resetRequested()
    }

    Timer {
        id: dragCommitTimer
        interval: root.dragCommitIntervalMs
        repeat: false
        onTriggered: root.flushQueuedDraggedValue()
    }

    Label {
        id: labelText
        x: theme.space6
        y: root.wideRailLayout ? theme.space2 : 0
        width: Math.max(1, root.wideRailLayout
                           ? root.compactValueX - x - theme.space8
                           : root.compactLabelWidth)
        height: root.wideRailLayout ? theme.controlHeightDense : root.resolvedSliderHeight
        verticalAlignment: Text.AlignVCenter
        text: root.label
        color: root.enabled
               ? (root.pressed ? theme.textStrong : theme.textMain)
               : theme.textFaint
        elide: Text.ElideNone
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: theme.typeCaption
        maximumLineCount: 1
        font.family: theme.fontUi
        font.pixelSize: theme.typeLabel
        font.weight: theme.weightControl
        font.capitalization: Font.MixedCase
        font.letterSpacing: 0

        HoverHandler {
            id: labelHover
        }

        Components.AppToolTip {
            targetItem: labelText
            visible: root.showFullLabelTooltip &&
                     (root.precisionResponse || labelText.contentWidth > labelText.width + 1 ||
                      (!root.enabled && root.disabledReason.length > 0)) &&
                     labelHover.hovered
            text: root.resolvedTooltipText
        }
    }

    ToneTextField {
        id: valueField
        objectName: root.objectName.length > 0 ? root.objectName + "ValueField" : ""
        accessibleLabel: root.label + " value"
        x: root.compactValueX
        y: root.wideRailLayout ? theme.space2
                               : Math.round((root.resolvedSliderHeight - height) / 2)
        width: root.valueColumnWidth
        height: root.wideRailLayout ? theme.controlHeightDense
                                    : Math.min(theme.controlHeightDense, root.resolvedSliderHeight)
        horizontalAlignment: Text.AlignRight
        leftPadding: theme.space4
        rightPadding: theme.space4
        enabled: root.enabled && root.valueInputEnabled
        Accessible.description: "Type a value for " + root.label
        text: root.valueText
        color: root.enabled ? theme.textStrong : theme.textFaint
        font.family: theme.fontUi
        font.pixelSize: theme.typeControl
        font.weight: theme.weightControl
        font.letterSpacing: 0
        hoverEnabled: true
        selectByMouse: true
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        background: Rectangle {
            color: valueField.activeFocus ? theme.fieldHoverFill
                   : valueField.hovered ? theme.alpha(theme.fieldFill, 0.8) : "transparent"
            border.width: valueField.activeFocus ? theme.focusBorderWidth
                          : valueField.hovered ? theme.hairlineWidth : 0
            border.color: valueField.activeFocus ? theme.focusRing : theme.alpha(theme.controlEdge, 0.72)
            radius: theme.radiusControl
        }
        onTextEdited: root.valueInputDirty = true
        onAccepted: root.commitValueText()
        onActiveFocusChanged: {
            if (!activeFocus)
                root.commitValueText()
        }
    }

    Components.ToolButton {
        objectName: root.objectName.length > 0 ? root.objectName + "ResetButton" : ""
        x: root.width - theme.space6 - width
        y: root.wideRailLayout ? theme.space2
                               : Math.round((root.resolvedSliderHeight - height) / 2)
        width: root.effectiveResetWidth
        height: Math.min(theme.controlHeightDense, root.resolvedSliderHeight)
        visible: root.showResetButton
        opacity: root.modified ? 1 : 0
        iconName: "reset"
        compact: true
        showLabel: false
        label: root.label.length > 0 ? "Reset " + root.label : "Reset value"
        active: false
        enabled: root.enabled && root.modified
        commandEnabled: root.enabled && root.modified
        disabledReason: root.modified ? "" : "Value is already at its default."
        onClicked: if (root.modified) root.resetRequested()
    }

    Slider {
        id: control
        x: root.wideRailLayout ? theme.space6 : root.compactTrackX
        y: root.wideRailLayout
           ? Math.max(theme.controlHeightDense + theme.space4,
                      root.resolvedSliderHeight - height - theme.space2)
           : 0
        width: root.wideRailLayout
               ? Math.max(1, root.width - theme.space12)
               : Math.max(1, root.compactValueX - x - theme.space8)
        height: root.wideRailLayout ? theme.scaled(24) : root.resolvedSliderHeight
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        Accessible.name: root.label
        Accessible.description: root.valueText
        Accessible.role: Accessible.Slider

                background: Item {
                    x: control.leftPadding
                    y: control.topPadding + control.availableHeight / 2 - height / 2
                    width: control.availableWidth
                    height: Math.max(1, Math.round(root.trackHeight * root.sliderScale))

                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: root.hasTrackGradient ? "transparent" : theme.alpha(theme.lineStrong, 0.30)
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
                               ? theme.alpha(theme.ink950, 0.08)
                               : "transparent"
                    }

                    Repeater {
                        model: root.hasTrackGradient ? 0 : 11
                        Rectangle {
                            required property int index
                            x: parent ? index * (parent.width - 1) / 10 : 0
                            y: -4
                            width: 1
                            height: index % 5 === 0 ? 4 : 2
                            color: theme.alpha(theme.textMuted, index % 5 === 0 ? 0.28 : 0.12)
                        }
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
                    x: control.leftPadding + root.displayedPosition * (control.availableWidth - width)
                    y: control.topPadding + control.availableHeight / 2 - height / 2
                    width: Math.round(root.thumbReservedSize * root.sliderScale)
                    height: width
                    radius: width / 2
                    rotation: 0
                    color: "transparent"

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * root.thumbVisualScale
                        height: width
                        radius: width / 2
                        color: root.pressed ? theme.brandPaper : "#D1D8E2"
                        border.width: control.visualFocus ? theme.focusBorderWidth : 1
                        border.color: control.visualFocus
                                      ? theme.focusRing
                                      : (root.pressed ? theme.alpha(root.accent, 0.9) : theme.alpha(theme.ink950, 0.62))

                        Behavior on width {
                            NumberAnimation { duration: theme.durationFast }
                        }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: root.pressed ? 4 : 3
                        height: width
                        radius: width / 2
                        color: "transparent"
                    }
                }

                MouseArea {
                    id: precisionPointer
                    objectName: root.objectName.length > 0 ? root.objectName + "PrecisionPointer" : ""
                    anchors.fill: parent
                    enabled: root.precisionResponse
                    preventStealing: true
                    property bool gestureActive: false
                    property real startValue: 0
                    property real pointerPosition: 0
                    property real lastX: 0
                    property bool fineMode: false
                    readonly property real travel: Math.max(1, width - control.handle.width)

                    function finish(cancel) {
                        if (!gestureActive)
                            return
                        gestureActive = false
                        if (cancel) {
                            dragCommitTimer.stop()
                            root.hasPendingDraggedValue = false
                            root.valueDragged(startValue)
                        } else {
                            root.flushQueuedDraggedValue()
                            dragCommitTimer.stop()
                        }
                        root.interactionFinished()
                    }

                    function publishPosition() {
                        const value = root.valueForPosition(pointerPosition)
                        const snapped = root.stepSize > 0 ? Math.round(value / root.stepSize) * root.stepSize : value
                        root.queueDraggedValue(root.clamp(snapped, root.from, root.to))
                    }

                    onPressed: function(mouse) {
                        control.forceActiveFocus()
                        startValue = control.value
                        pointerPosition = root.positionForValue(startValue)
                        lastX = mouse.x
                        fineMode = (mouse.modifiers & Qt.ShiftModifier) !== 0
                        gestureActive = true
                        root.interactionStarted()
                        const handleCenter = control.handle.width / 2 + root.displayedPosition * travel
                        if (Math.abs(mouse.x - handleCenter) > control.handle.width && !fineMode) {
                            pointerPosition = root.clamp((mouse.x - control.handle.width / 2) / travel, 0, 1)
                            if (control.mirrored)
                                pointerPosition = 1 - pointerPosition
                            publishPosition()
                        }
                    }
                    onPositionChanged: function(mouse) {
                        if (!gestureActive)
                            return
                        const dx = mouse.x - lastX
                        lastX = mouse.x
                        const fine = (mouse.modifiers & Qt.ShiftModifier) !== 0
                        if (fine !== fineMode) {
                            fineMode = fine
                            return
                        }
                        pointerPosition = root.clamp(pointerPosition + dx / travel * (control.mirrored ? -1 : 1) * (fine ? 0.1 : 1), 0, 1)
                        publishPosition()
                    }
                    onReleased: finish(false)
                    onCanceled: finish(true)
                }
                Keys.onEscapePressed: function(event) {
                    if (precisionPointer.gestureActive) {
                        precisionPointer.finish(true)
                        event.accepted = true
                    }
                }

                onMoved: root.queueDraggedValue(control.value)
                onPressedChanged: {
                    if (pressed) {
                        root.interactionStarted()
                    } else {
                        root.flushQueuedDraggedValue()
                        root.interactionFinished()
                    }
                }
    }
}
