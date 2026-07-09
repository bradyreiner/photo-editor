import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    property string layerStackLabel: "Layers / Selections"
    property string layerIconName: "media"
    property string layerName: "Base image"
    property string layerStateText: "Direct adjustment"
    property string maskName: "No active selection"
    property string maskStateText: ""
    property int maskSetCount: 0
    property string maskChipText: "Active selection"
    property string operation: "add"
    property string previewLabel: "Overlay"
    property bool previewActive: true
    property string brushPanelContract: "size-flow-hardness-density-opacity|compact:v2|visible:false|tuning-menu:true|inspector-preserved:true"
    property string brushSizeText: "Size 6%"
    property string brushFlowText: "Flow 100%"
    property string brushHardnessText: "Hard 45%"
    property string brushDensityText: "Density 100%"
    property string brushOpacityText: "Opacity 100%"
    property string exposureLabel: "Layer Exposure"
    property real exposureValue: 0
    property string exposureValueText: "0.00 EV"
    property string temperatureLabel: "Layer Temp"
    property real temperatureValue: 0
    property string temperatureValueText: "0%"
    property color accent: theme.accentCool400
    readonly property string contextContract: "brush-context-panel:v1|component:true|target-summary:true|mask-summary:true|hidden-advanced-preserved:true|object-names:preserved|no-elide:true"

    signal showLayersRequested()
    signal addRequested()
    signal eraseRequested()
    signal overlayToggleRequested()
    signal densityDownRequested()
    signal densityUpRequested()
    signal opacityDownRequested()
    signal opacityUpRequested()
    signal exposureDragged(real value)
    signal temperatureDragged(real value)

    Theme { id: theme }

    implicitHeight: brushContextContent.implicitHeight + 14
    radius: 4
    color: theme.alpha(theme.ink950, 0.78)
    border.width: theme.chromeBorderWidth
    border.color: theme.alpha(root.accent, 0.34)
    clip: true

    ColumnLayout {
        id: brushContextContent
        anchors.fill: parent
        anchors.margins: 7
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                objectName: "brushContextLayerStackLabel"
                Layout.fillWidth: true
                text: root.layerStackLabel
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 9
                font.weight: Font.DemiBold
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 7
                elide: Text.ElideNone
            }

            Components.MiniButton {
                objectName: "brushContextShowLayersButton"
                text: "Open Layers"
                role: "segmented"
                active: false
                tooltip: "Open the full Layers inspector without leaving brush mode."
                onClicked: root.showLayersRequested()
            }
        }

        Rectangle {
            objectName: "brushContextLayerCard"
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            radius: 3
            color: theme.alpha(theme.panelLift, 0.88)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha(theme.glassEdge, 0.42)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 7

                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 3
                    color: theme.alpha(theme.accentCool400, 0.12)
                    border.width: 1
                    border.color: theme.alpha(theme.textMain, 0.22)

                    Components.AppIcon {
                        anchors.centerIn: parent
                        width: 13
                        height: 13
                        name: root.layerIconName
                        color: theme.textMain
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Label {
                        objectName: "brushContextLayerNameLabel"
                        Layout.fillWidth: true
                        text: root.layerName
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideNone
                    }

                    Label {
                        objectName: "brushContextLayerStateLabel"
                        Layout.fillWidth: true
                        text: root.layerStateText
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 6
                        elide: Text.ElideNone
                    }
                }
            }
        }

        Rectangle {
            objectName: "brushContextMaskCard"
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            radius: 3
            color: theme.alpha(root.accent, 0.075)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha(root.accent, 0.44)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 7
                spacing: 2

                Label {
                    objectName: "brushContextMaskNameLabel"
                    Layout.fillWidth: true
                    text: root.maskName
                    color: root.accent
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }

                Label {
                    objectName: "brushContextMaskStateLabel"
                    Layout.fillWidth: true
                    text: root.maskStateText
                    color: theme.textMain
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 7
                    elide: Text.ElideNone
                }
            }
        }

        RowLayout {
            objectName: "brushContextMaskStackRow"
            visible: false
            Layout.fillWidth: true
            spacing: 5

            Label {
                objectName: "brushContextMaskCountLabel"
                text: root.maskSetCount + " selection sets"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 9
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 7
                elide: Text.ElideNone
            }

            Rectangle {
                objectName: "brushContextMaskChip"
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                radius: 3
                color: theme.alpha(root.accent, 0.16)
                border.width: 1
                border.color: root.accent

                Label {
                    anchors.fill: parent
                    anchors.margins: 5
                    text: root.maskChipText
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 7
                    elide: Text.ElideNone
                }
            }
        }

        RowLayout {
            visible: false
            Layout.fillWidth: true
            spacing: 6

            Components.MiniButton {
                objectName: "brushContextAddButton"
                Layout.fillWidth: true
                text: "Add"
                iconName: "brush-tool"
                role: "segmented"
                active: root.operation === "add"
                accent: root.accent
                onClicked: root.addRequested()
            }

            Components.MiniButton {
                objectName: "brushContextEraseButton"
                Layout.fillWidth: true
                text: "Erase"
                iconName: "brush-subtract"
                role: "segmented"
                active: root.operation === "subtract"
                accent: "#d98962"
                onClicked: root.eraseRequested()
            }

            Components.MiniButton {
                objectName: "brushContextOverlayToggle"
                Layout.fillWidth: true
                text: root.previewLabel
                role: "segmented"
                active: root.previewActive
                tooltip: "Toggle the selection preview overlay."
                onClicked: root.overlayToggleRequested()
            }
        }

        Rectangle {
            objectName: "brushContextCompactBrushPanel"
            readonly property string brushPanelContract: root.brushPanelContract
            visible: false
            Layout.fillWidth: true
            Layout.preferredHeight: 82
            radius: 3
            color: theme.alpha(theme.ink950, 0.54)
            border.width: 1
            border.color: theme.alpha(theme.glassEdge, 0.24)

            GridLayout {
                anchors.fill: parent
                anchors.margins: 7
                columns: 3
                columnSpacing: 6
                rowSpacing: 5

                Label {
                    objectName: "brushContextBrushSizeLabel"
                    Layout.fillWidth: true
                    text: root.brushSizeText
                    color: theme.textMain
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 7
                    elide: Text.ElideNone
                }

                Label {
                    objectName: "brushContextBrushFlowLabel"
                    Layout.fillWidth: true
                    text: root.brushFlowText
                    color: theme.textMain
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 7
                    elide: Text.ElideNone
                }

                Label {
                    objectName: "brushContextBrushHardnessLabel"
                    Layout.fillWidth: true
                    text: root.brushHardnessText
                    color: theme.textMain
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 7
                    elide: Text.ElideNone
                }

                RowLayout {
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    spacing: 5

                    Label {
                        objectName: "brushContextBrushDensityLabel"
                        Layout.fillWidth: true
                        text: root.brushDensityText
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 7
                        elide: Text.ElideNone
                    }

                    Components.MiniButton {
                        objectName: "brushContextDensityDownButton"
                        square: true
                        iconName: "minus"
                        role: "icon"
                        tooltip: "Lower mask density."
                        onClicked: root.densityDownRequested()
                    }

                    Components.MiniButton {
                        objectName: "brushContextDensityUpButton"
                        square: true
                        iconName: "plus"
                        role: "icon"
                        tooltip: "Raise mask density."
                        onClicked: root.densityUpRequested()
                    }
                }

                RowLayout {
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    spacing: 5

                    Label {
                        objectName: "brushContextBrushOpacityLabel"
                        Layout.fillWidth: true
                        text: root.brushOpacityText
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 7
                        elide: Text.ElideNone
                    }

                    Components.MiniButton {
                        objectName: "brushContextOpacityDownButton"
                        square: true
                        iconName: "minus"
                        role: "icon"
                        tooltip: "Lower mask opacity."
                        onClicked: root.opacityDownRequested()
                    }

                    Components.MiniButton {
                        objectName: "brushContextOpacityUpButton"
                        square: true
                        iconName: "plus"
                        role: "icon"
                        tooltip: "Raise mask opacity."
                        onClicked: root.opacityUpRequested()
                    }
                }
            }
        }

        Components.InspectorSlider {
            objectName: "brushContextExposureSlider"
            visible: false
            controlHeight: 34
            label: root.exposureLabel
            from: -2
            to: 2
            stepSize: 0.01
            defaultValue: 0
            value: root.exposureValue
            valueText: root.exposureValueText
            onValueDragged: function(value) { root.exposureDragged(value) }
        }

        Components.InspectorSlider {
            objectName: "brushContextTemperatureSlider"
            visible: false
            controlHeight: 34
            label: root.temperatureLabel
            from: -100
            to: 100
            stepSize: 1
            defaultValue: 0
            value: root.temperatureValue
            valueText: root.temperatureValueText
            trackColors: ["#4FB5FF", "#F2C35B"]
            onValueDragged: function(value) { root.temperatureDragged(value) }
        }
    }
}
