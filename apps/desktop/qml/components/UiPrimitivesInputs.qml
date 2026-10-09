import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root

    Theme { id: theme }

    property bool inspectorSectionCompareActive: false
    property bool inspectorSectionCompareHeld: false
    property bool inspectorSectionExpanded: true
    property bool collapsibleCompareActive: false
    property bool collapsibleCompareHeld: false
    property bool collapsibleExpanded: true
    property string activeSliderInteraction: ""
    property real lastSliderValue: 0

    function resetExamples() {
        sampleNameField.text = "Morning light"
        sampleNotesArea.text = "Warm window light, neutral wall, soft shadow detail."
        sampleColorCombo.currentIndex = 2
        sampleMetadataCheck.checked = true
        sampleFrameRate.value = 24
        exposureSlider.value = 0
        compactExposureSlider.value = 0
        temperatureSlider.value = 0
        sectionTargetField.text = "Adjustment target"
        collapsibleTargetField.text = "Local group value"
        root.inspectorSectionExpanded = true
        inspectorSectionExample.expanded = true
        root.collapsibleExpanded = true
        root.inspectorSectionCompareActive = false
        root.collapsibleCompareActive = false
        root.inspectorSectionCompareHeld = false
        root.collapsibleCompareHeld = false
        root.activeSliderInteraction = ""
        root.lastSliderValue = 0
    }

    spacing: theme.gapUnrelated

    RowLayout {
        Layout.fillWidth: true
        spacing: theme.gapControl

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space2

            Label {
                Layout.fillWidth: true
                text: "Inputs and controls"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeSection
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: "Live component samples. Values stay in this gallery and reset locally."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
            }
        }

        ActionButton {
            objectName: "uiPrimitivesResetAllButton"
            text: "Reset samples"
            iconName: "reset"
            compact: true
            tooltipText: "Restore the gallery examples to their starting values."
            onClicked: root.resetExamples()
        }
    }

    ColumnLayout {
        id: galleryContent
        Layout.fillWidth: true
        spacing: theme.gapUnrelated

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.gapDense

            Label {
                text: "Input controls"
                color: theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: "Text, choice, numeric, and boolean inputs use the same field surface and focus treatment."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= theme.scaled(760) ? 2 : 1
                uniformCellWidths: true
                columnSpacing: theme.gapControl
                rowSpacing: theme.gapControl

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.gapDense

                    Label {
                        text: "Text field"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                    }

                    ToneTextField {
                        id: sampleNameField
                        objectName: "uiPrimitivesTextField"
                        Layout.fillWidth: true
                        accessibleLabel: "Sample label"
                        placeholderText: "Enter a short label"
                        text: "Morning light"
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Single-line entry with an explicit accessible name."
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.gapDense

                    Label {
                        text: "Choice"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                    }

                    ToneComboBox {
                        id: sampleColorCombo
                        objectName: "uiPrimitivesComboBox"
                        Layout.fillWidth: true
                        accessibleLabel: "Working color space"
                        model: ["sRGB", "Display P3", "Rec.709", "ACEScg"]
                        currentIndex: 2
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Keyboard-selectable options with a clamped popup and a visible selected marker."
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.gapDense

                    Label {
                        text: "Numeric stepper"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                    }

                    ToneSpinBox {
                        id: sampleFrameRate
                        objectName: "uiPrimitivesSpinBox"
                        Layout.fillWidth: true
                        accessibleLabel: "Sequence frame rate"
                        accessibleDescription: "Frames per second for this local example only."
                        valueUnit: "frames per second"
                        from: 1
                        to: 120
                        stepSize: 1
                        value: 24
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Numeric entry and arrow stepping; range is 1–120."
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.gapDense

                    Label {
                        text: "Boolean option"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                    }

                    ToneCheckBox {
                        id: sampleMetadataCheck
                        objectName: "uiPrimitivesCheckBox"
                        Layout.fillWidth: true
                        accessibleLabel: "Use camera metadata"
                        accessibleDescription: "Enable automatic camera metadata matching in this local example."
                        allowLabelWrap: true
                        text: "Use camera metadata when an exact profile is available"
                        checked: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "The label remains readable at narrow widths; keyboard focus and checked state are explicit."
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.gapDense

                Label {
                    text: "Multiline text"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeLabel
                }

                ToneTextArea {
                    id: sampleNotesArea
                    objectName: "uiPrimitivesTextArea"
                    Layout.fillWidth: true
                    accessibleLabel: "Sample editing note"
                    placeholderText: "Add a note"
                    text: "Warm window light, neutral wall, soft shadow detail."
                    wrapMode: TextEdit.Wrap
                }
            }

            Label {
                objectName: "uiPrimitivesInputValueSummary"
                Layout.fillWidth: true
                text: "Current samples · " + sampleColorCombo.currentText + " · "
                      + sampleFrameRate.value + " fps · camera metadata "
                      + (sampleMetadataCheck.checked ? "on" : "off")
                color: theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: theme.typeCaption
                wrapMode: Text.Wrap
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.gapDense

            Label {
                text: "Inspector sliders"
                color: theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: "Numeric entry, reset, centered ranges, and color tracks use the same slider component. Drag edits are locally observed; no document is attached."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= theme.scaled(760) ? 2 : 1
                columnSpacing: theme.gapControl
                rowSpacing: theme.gapControl

                InspectorSlider {
                    id: exposureSlider
                    objectName: "uiPrimitivesExposureSlider"
                    Layout.fillWidth: true
                    label: "Exposure · standard"
                    from: -5
                    to: 5
                    stepSize: 0.01
                    precisionResponse: true
                    value: 0
                    defaultValue: 0
                    valueText: Number(value).toFixed(2) + " EV"
                    onInteractionStarted: root.activeSliderInteraction = label
                    onInteractionFinished: root.activeSliderInteraction = ""
                    onValueDragged: function(nextValue) {
                        value = nextValue
                        root.lastSliderValue = nextValue
                    }
                    onResetRequested: {
                        value = defaultValue
                        root.lastSliderValue = 0
                    }
                }

                InspectorSlider {
                    id: compactExposureSlider
                    objectName: "uiPrimitivesCompactExposureSlider"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    controlHeight: theme.iconButtonSizeCompact
                    label: "Exposure · compact"
                    from: -5
                    to: 5
                    stepSize: 0.01
                    precisionResponse: true
                    forceCompact: true
                    value: 0
                    defaultValue: 0
                    valueText: Number(value).toFixed(2) + " EV"
                    onInteractionStarted: root.activeSliderInteraction = label
                    onInteractionFinished: root.activeSliderInteraction = ""
                    onValueDragged: function(nextValue) {
                        value = nextValue
                        root.lastSliderValue = nextValue
                    }
                    onResetRequested: {
                        value = defaultValue
                        root.lastSliderValue = 0
                    }
                }
            }

            InspectorSlider {
                id: temperatureSlider
                objectName: "uiPrimitivesTemperatureSlider"
                Layout.fillWidth: true
                label: "Temperature"
                from: -100
                to: 100
                stepSize: 1
                valueInputMode: "kelvin"
                value: 0
                defaultValue: 0
                valueText: Math.round(5500 + value * (value >= 0 ? 65 : 35)) + "K"
                trackColors: ["#4FB5FF", "#F2C35B"]
                onInteractionStarted: root.activeSliderInteraction = label
                onInteractionFinished: root.activeSliderInteraction = ""
                onValueDragged: function(nextValue) {
                    value = nextValue
                    root.lastSliderValue = nextValue
                }
                onResetRequested: {
                    value = defaultValue
                    root.lastSliderValue = 0
                }
            }

            Label {
                objectName: "uiPrimitivesSliderEventSummary"
                Layout.fillWidth: true
                text: root.activeSliderInteraction.length > 0
                      ? "Editing " + root.activeSliderInteraction + "…"
                      : "Last committed slider value · " + Number(root.lastSliderValue).toFixed(2)
                color: theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: theme.typeCaption
                wrapMode: Text.Wrap
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.gapDense

            Label {
                text: "Inspector group comparison"
                color: theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: "Compare the shared divider-based section header with the older hover-card collapsible header. Both examples reset local values only."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WordWrap
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.width >= theme.scaled(760) ? 2 : 1
                columnSpacing: theme.gapUnrelated
                uniformCellWidths: true
                rowSpacing: theme.gapDense

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: theme.gapDense

                    InspectorSection {
                        id: inspectorSectionExample
                        objectName: "uiPrimitivesInspectorSection"
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.minimumHeight: theme.scaled(110)
                        Layout.preferredHeight: theme.scaled(120)
                        title: "InspectorSection"
                        subtitle: "Divider header · expandable · compare and reset hooks"
                        expandable: true
                        expanded: root.inspectorSectionExpanded
                        changed: sectionTargetField.text !== "Adjustment target"
                        beforeAfterAvailable: true
                        beforeAfterActive: root.inspectorSectionCompareActive
                        beforeAfterObjectName: "uiPrimitivesInspectorSectionCompareButton"
                        beforeAfterTooltip: "Toggle comparison for the local section sample."
                        resetAvailable: true
                        resetObjectName: "uiPrimitivesInspectorSectionResetButton"
                        resetTooltip: "Restore the local section field."
                        onToggled: root.inspectorSectionExpanded = inspectorSectionExample.expanded
                        onBeforeAfterRequested: root.inspectorSectionCompareActive = !root.inspectorSectionCompareActive
                        onBeforeAfterHoldRequested: function(held) { root.inspectorSectionCompareHeld = held }
                        onResetRequested: {
                            sectionTargetField.text = "Adjustment target"
                            root.inspectorSectionExpanded = true
                            inspectorSectionExample.expanded = true
                            root.inspectorSectionCompareActive = false
                        }

                        ToneTextField {
                            id: sectionTargetField
                            objectName: "uiPrimitivesInspectorSectionField"
                            Layout.fillWidth: true
                            accessibleLabel: "Inspector section local value"
                            text: "Adjustment target"
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Choose this for flat inspector sections with shared before/after, reset, optional menu, and accessible expansion behavior."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: theme.gapDense

                    StudioCollapsibleGroup {
                        id: collapsibleGroupExample
                        objectName: "uiPrimitivesStudioCollapsibleGroup"
                        Layout.minimumHeight: theme.controlHeightDense + theme.space4 + theme.controlHeightTall
                        Layout.preferredHeight: theme.controlHeightDense + theme.space4 + theme.controlHeightTall + theme.gapDense
                        title: "StudioCollapsibleGroup"
                        expanded: root.collapsibleExpanded
                        resetVisible: true
                        resetObjectName: "uiPrimitivesStudioCollapsibleResetButton"
                        compareVisible: true
                        compareObjectName: "uiPrimitivesStudioCollapsibleCompareButton"
                        compareActive: root.collapsibleCompareActive
                        onToggled: root.collapsibleExpanded = !root.collapsibleExpanded
                        onResetRequested: {
                            collapsibleTargetField.text = "Local group value"
                            root.collapsibleExpanded = true
                            root.collapsibleCompareActive = false
                        }
                        onCompareRequested: root.collapsibleCompareActive = !root.collapsibleCompareActive
                        onCompareHoldRequested: function(held) { root.collapsibleCompareHeld = held }

                        ToneTextField {
                            id: collapsibleTargetField
                            objectName: "uiPrimitivesStudioCollapsibleField"
                            Layout.fillWidth: true
                            accessibleLabel: "Collapsible group local value"
                            text: "Local group value"
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Choose this for compact nested controls that need an independent disclosure. Its header surface and spacing differ from InspectorSection."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Label {
                objectName: "uiPrimitivesGroupStateSummary"
                Layout.fillWidth: true
                text: "Section · " + (inspectorSectionExample.expanded ? "open" : "closed")
                      + " · comparison " + (root.inspectorSectionCompareHeld ? "held" : root.inspectorSectionCompareActive ? "on" : "off")
                      + "    /    Collapsible · " + (root.collapsibleExpanded ? "open" : "closed")
                      + " · comparison " + (root.collapsibleCompareHeld ? "held" : root.collapsibleCompareActive ? "on" : "off")
                color: theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: theme.typeCaption
                wrapMode: Text.Wrap
            }
        }
    }
}
