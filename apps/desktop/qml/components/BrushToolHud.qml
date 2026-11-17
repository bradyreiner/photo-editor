import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Item {
    id: root

    property bool embedded: false
    property string operation: "add"
    property string statusText: ""
    property string brushPageContract: ""
    property string brushDefaultContract: ""
    property string sizePercentText: "6"
    property real flowValue: 1
    property real hardnessValue: 0.8
    readonly property bool inlineTuningVisible: true
    readonly property string tipPreviewAccessibleSummary: "Normalized brush tip visual. Hardness " +
        Math.round(root.hardnessValue * 100) + "% controls edge falloff; Flow " +
        Math.round(root.flowValue * 100) + "% controls sample opacity."
    readonly property string tipPreviewTooltipText: root.tipPreviewAccessibleSummary +
        " Stroke buildup varies with movement and other brush settings."
    property bool sizeDragActive: false
    property var modeMenuItems: []
    property var tuningMenuItems: []
    property var acceptSizeText: null
    property var commitSizeText: null
    property color accent: theme.accentCool400
    readonly property string hudContract: "specialized-tool-hud:v5|tool:brush|component:BrushToolHud|surface:graphite|mode:add-erase-visible|tuning:size-hardness-flow-horizontal|responsive-wrap:true|advanced:inspector-or-menus|commit-action:true|shallow-corners:true|no-clipping:true"
    readonly property string modularityContract: "brush-tool-hud-component:v3|owns:mode-size-quick-tuning-done|precision:size-percent-field|advanced:inspector-or-shortcuts|signals:mode-tuning-size-done|object-names:preserved|workspace-state:external"

    signal modeClicked
    signal modeMenuItemTriggered(string key)
    signal addRequested
    signal eraseRequested
    signal tuningClicked
    signal tuningMenuItemTriggered(string key)
    signal resetDefaultsRequested
    signal doneRequested
    signal sizeDownRequested
    signal sizeUpRequested
    signal flowDownRequested
    signal flowUpRequested
    signal hardnessDownRequested
    signal hardnessUpRequested
    signal sizeDragPressed(real sceneX)
    signal sizeDragMoved(real sceneX)
    signal sizeDragReleased
    signal sizeTextAccepted(string text)
    signal sizeTextEditingFinished(string text)
    signal flowValueEdited(real value)
    signal hardnessValueEdited(real value)

    Theme {
        id: theme
    }

    readonly property string resolvedStatusText: {
        const parts = String(root.statusText || "").split(/\s{2,}/);
        const useful = [];
        for (let i = 0; i < parts.length; ++i) {
            const part = parts[i].trim();
            if (part.length === 0 || /^(Size|Flow|Hard)\b/i.test(part))
                continue;
            useful.push(part);
        }
        return useful.join("  ");
    }

    implicitWidth: theme.scaled(root.embedded ? 320 : 950)
    Layout.minimumWidth: root.embedded ? 0 : implicitWidth
    implicitHeight: hudSurface.implicitHeight
    height: implicitHeight

    Rectangle {
        id: hudSurface
        objectName: "brushHudSurface"
        anchors.fill: parent
        implicitHeight: controls.implicitHeight + (root.embedded ? 0 : theme.space16)
        radius: theme.radiusPanel
        color: root.embedded ? "transparent" : theme.alpha(theme.surfaceTop, 0.96)
        border.width: root.embedded ? 0 : theme.hairlineWidth
        border.color: theme.alpha(theme.glassEdge, 0.72)

        MouseArea {
            anchors.fill: parent
        }
    }

    ColumnLayout {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.embedded ? 0 : theme.space8
        spacing: theme.space8

        GridLayout {
            id: brushCommandControls
            Layout.fillWidth: true
            columns: root.embedded ? 3 : 5
            columnSpacing: theme.space2
            rowSpacing: theme.space4

            Components.MiniButton {
                objectName: "brushHudAddButton"
                Layout.fillWidth: true
                text: qsTr("Add")
                role: "segmented"
                active: root.operation === "add"
                accent: theme.accentCool400
                minimumReadableWidth: theme.scaled(58)
                tooltip: qsTr("Paint into the active selection.")
                onClicked: root.addRequested()
            }
            Components.MiniButton {
                objectName: "brushHudEraseButton"
                Layout.fillWidth: true
                text: qsTr("Erase")
                role: "segmented"
                active: root.operation === "subtract"
                accent: theme.error
                minimumReadableWidth: theme.scaled(60)
                tooltip: qsTr("Erase from the active selection.")
                onClicked: root.eraseRequested()
            }

            Components.MenuButton {
                objectName: "brushHudModeMenuButton"
                Layout.fillWidth: true
                Layout.preferredWidth: theme.scaled(100)
                Layout.alignment: Qt.AlignVCenter
                text: qsTr("Brush")
                iconName: "brush-tool"
                compact: true
                primaryOpensMenu: true
                active: false
                accent: root.accent
                menuWidth: 312
                menuItemMaxLines: 3
                menuItems: root.modeMenuItems
                tooltipText: qsTr("Choose a brush preset and painting mode.")
                menuTooltipText: qsTr("Brush and mode")
                onClicked: root.modeClicked()
                onMenuItemTriggered: function (key) {
                    root.modeMenuItemTriggered(key);
                }
            }

            Components.MiniButton {
                objectName: "brushHudResetDefaultsButton"
                Layout.alignment: Qt.AlignCenter
                readonly property string brushDefaultContract: root.brushDefaultContract
                square: true
                iconName: "reset"
                role: "secondary"
                tooltip: "Reset brush size, flow, hardness, density, opacity, spacing, smoothing, and pressure response."
                onClicked: root.resetDefaultsRequested()
            }

            Components.MiniButton {
                objectName: "brushHudDoneButton"
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                minimumReadableWidth: theme.scaled(78)
                text: qsTr("Done")
                iconName: "check"
                role: "primary"
                onClicked: root.doneRequested()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space4
            Components.BrushPresetPicker {
                id: selectionPresets
                objectName: "selectionBrushPresets"
                Layout.fillWidth: true
                presetFamily: "selection"
                currentSettings: ({sizePercent: Number(root.sizePercentText),
                    hardness: root.hardnessValue, flow: root.flowValue})
                onPresetSelected: function(settings) {
                    if (settings.sizePercent !== undefined && isFinite(Number(settings.sizePercent))) {
                        const text = String(Math.max(1, Math.min(35, Math.round(Number(settings.sizePercent)))))
                        if (root.acceptSizeText !== null) root.acceptSizeText(text)
                        else root.sizeTextAccepted(text)
                    }
                    if (settings.hardness !== undefined && isFinite(Number(settings.hardness)))
                        root.hardnessValueEdited(Math.max(0, Math.min(1, Number(settings.hardness))))
                    if (settings.flow !== undefined && isFinite(Number(settings.flow)))
                        root.flowValueEdited(Math.max(0.05, Math.min(1, Number(settings.flow))))
                }
            }
            Components.MiniButton {
                objectName: "selectionBrushPreviewToggle"
                text: qsTr("Preview")
                role: "segmented"
                active: selectionPresets.previewVisible
                tooltip: qsTr("Show or hide the tip practice area.")
                onClicked: selectionPresets.previewVisible = !selectionPresets.previewVisible
            }
        }
        GridLayout {
            id: brushQuickControls
            Layout.fillWidth: true
            // A sidebar needs complete label/value rows. Keep the same tuning
            // controls full-width until two readable columns fit side by side.
            columns: root.embedded ? (width < theme.scaled(560) ? 1 : 2) : 3
            columnSpacing: theme.space8
            rowSpacing: theme.space8

            ColumnLayout {
                Layout.fillWidth: true
                Layout.columnSpan: root.embedded ? brushQuickControls.columns : 1
                id: brushSizeControls
                Layout.minimumWidth: theme.scaled(120)
                spacing: theme.space2

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Size")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: theme.weightControl
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    ToneTextField {
                        numericEntry: true
                        id: brushHudSizeField
                        objectName: "brushHudSizeField"
                        accessibleLabel: qsTr("Selection brush size percent")
                        Layout.preferredWidth: theme.scaled(48)
                        Layout.preferredHeight: theme.controlHeightDense
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        selectByMouse: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        validator: IntValidator {
                            bottom: 1
                            top: 35
                        }
                        color: theme.textMain
                        font.family: theme.fontNumeric
                        font.features: theme.numericFontFeatures
                        font.pixelSize: theme.typeControl
                        font.weight: theme.weightControl
                        leftPadding: theme.space4
                        rightPadding: theme.space4
                        background: InputFieldSurface { inputControl: parent }
                        onAccepted: {
                            root.sizeTextAccepted(text);
                            if (root.acceptSizeText === null || root.acceptSizeText(text))
                                focus = false;
                            else {
                                text = root.sizePercentText;
                                focus = false;
                            }
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus) {
                                root.sizeTextEditingFinished(text);
                                if (root.commitSizeText !== null)
                                    root.commitSizeText(text);
                            }
                        }
                    }
                    Binding {
                        target: brushHudSizeField
                        property: "text"
                        value: root.sizePercentText
                        when: !brushHudSizeField.activeFocus
                    }
                    Label {
                        text: "%"
                        color: theme.textMuted
                        font.family: theme.fontNumeric
                        font.features: theme.numericFontFeatures
                        font.pixelSize: theme.typeCaption
                    }
                    Canvas {
                        id: brushHudTipPreview
                        objectName: "brushHudTipPreview"
                        Layout.preferredWidth: theme.controlHeightDense
                        Layout.preferredHeight: theme.controlHeightDense
                        Layout.alignment: Qt.AlignVCenter
                        readonly property real previewHardness: root.hardnessValue
                        readonly property real previewFlow: root.flowValue
                        readonly property color previewAccent: root.accent
                        Accessible.name: qsTr("Brush tip preview")
                        Accessible.description: root.tipPreviewAccessibleSummary
                        Accessible.role: Accessible.StaticText
                        onPreviewHardnessChanged: requestPaint()
                        onPreviewFlowChanged: requestPaint()
                        onPreviewAccentChanged: requestPaint()

                        onPaint: {
                            const context = getContext("2d")
                            context.save()
                            context.clearRect(0, 0, width, height)

                            const radius = Math.max(1, Math.min(width, height) * 0.42)
                            const centerX = width * 0.5
                            const centerY = height * 0.5
                            const hardness = Math.max(0, Math.min(1, previewHardness))
                            const flow = Math.max(0, Math.min(1, previewFlow))
                            const color = previewAccent.toString()

                            context.globalAlpha = flow
                            context.beginPath()
                            context.arc(centerX, centerY, radius, 0, Math.PI * 2, false)
                            if (hardness >= 0.999) {
                                context.fillStyle = color
                            } else {
                                const falloff = context.createRadialGradient(
                                    centerX, centerY, 0, centerX, centerY, radius)
                                falloff.addColorStop(0, color)
                                if (hardness > 0)
                                    falloff.addColorStop(hardness, color)
                                falloff.addColorStop(1, "rgba(0, 0, 0, 0)")
                                context.fillStyle = falloff
                            }
                            context.fill()
                            context.restore()
                        }

                        HoverHandler { id: brushHudTipPreviewHover }
                        Components.AppToolTip {
                            targetItem: brushHudTipPreview
                            visible: brushHudTipPreviewHover.hovered
                            text: root.tipPreviewTooltipText
                        }
                    }
                }

                Components.ToolValueSlider {
                    objectName: "brushHudSizeVerticalSlider"
                    Layout.fillWidth: true
                    minimumReadableWidth: theme.scaled(96)
                    label: qsTr("Selection brush size percent")
                    tooltipText: qsTr("Selection brush size: %1%").arg(root.sizePercentText)
                    from: 1
                    to: 35
                    stepSize: 1
                    value: Number(root.sizePercentText)
                    onMoved: {
                        if (root.acceptSizeText !== null)
                            root.acceptSizeText(String(Math.round(value)));
                        else
                            root.sizeTextAccepted(String(Math.round(value)));
                    }
                }
            }

            ColumnLayout {
                id: brushHardnessControls
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: theme.space2

                Components.InspectorSlider {
                    objectName: "brushHudHardnessVerticalSlider"
                    Layout.fillWidth: true
                    controlHeight: theme.controlHeightDense
                    forceCompact: true
                    showResetButton: false
                    labelColumnWidth: theme.scaled(76)
                    label: qsTr("Hardness")
                    from: 0
                    to: 1
                    stepSize: 0.01
                    value: root.hardnessValue
                    valueText: Math.round(root.hardnessValue * 100) + "%"
                    valueInputMode: "percentNormalized"
                    onValueDragged: function(nextValue) { root.hardnessValueEdited(nextValue) }
                }
            }

            ColumnLayout {
                id: brushFlowControls
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: theme.space2

                Components.InspectorSlider {
                    objectName: "brushHudFlowVerticalSlider"
                    Layout.fillWidth: true
                    controlHeight: theme.controlHeightDense
                    forceCompact: true
                    showResetButton: false
                    labelColumnWidth: theme.scaled(76)
                    label: qsTr("Flow")
                    from: 0.05
                    to: 1
                    stepSize: 0.01
                    value: root.flowValue
                    valueText: Math.round(root.flowValue * 100) + "%"
                    valueInputMode: "percentNormalized"
                    onValueDragged: function(nextValue) { root.flowValueEdited(nextValue) }
                }
            }
        }

        Components.BrushStrokePreview {
            objectName: "selectionBrushTipPreview"
            Layout.fillWidth: true
            visible: selectionPresets.previewVisible
            diameter: Number(root.sizePercentText) * 6
            hardness: root.hardnessValue
            flow: root.flowValue
            ink: root.accent
        }

        Label {
            objectName: "brushHudStatusLabel"
            Layout.fillWidth: true
            visible: root.resolvedStatusText.length > 0
            text: root.resolvedStatusText
            color: theme.textMuted
            font.family: theme.fontNumeric
            font.features: theme.numericFontFeatures
            font.pixelSize: theme.typeMicro
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
        }

        Components.MenuButton {
            objectName: "brushHudTuningMenuButton"
            Layout.preferredWidth: theme.scaled(104)
            visible: false
            text: "Tune"
            iconName: "adjust"
            compact: true
            active: false
            accent: root.accent
            menuWidth: 300
            menuItemMaxLines: 3
            menuItems: root.tuningMenuItems
            tooltipText: "Adjust brush size, flow, or hardness."
            menuTooltipText: "Quick brush tuning"
            onClicked: root.tuningClicked()
            onMenuItemTriggered: function (key) {
                root.tuningMenuItemTriggered(key);
            }
        }

    }
}
