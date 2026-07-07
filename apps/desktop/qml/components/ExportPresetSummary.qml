import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string format: ""
    property string bitDepth: ""
    property string maxEdge: ""
    property string quality: ""
    property string description: ""
    readonly property int metricColumns: width < 280 ? 2 : 4
    readonly property var metricItems: [
        { label: "Format", value: format },
        { label: "Depth", value: bitDepth },
        { label: "Max Size", value: maxEdge },
        { label: "Quality", value: quality }
    ]
    readonly property string exportPresetSummaryContract: "export-preset-summary:v1|metric-fit:true|description-wrap:true|adaptive-grid:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|metric-fit:true|description-wrap:true|tooltip-full:true|accessibility-full:true"

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: presetInfoColumn.implicitHeight + theme.space16
    radius: theme.radius12
    color: theme.alpha(theme.ink900, 0.5)
    border.width: theme.chromeBorderWidth
    border.color: theme.alpha(theme.lineSoft, 0.5)
    clip: true
    Accessible.name: "Export preset. " +
                     root.metricItems.map(function(item) {
                         return item.label + " " + item.value
                     }).join(". ") +
                     ". " + root.description
    Accessible.role: Accessible.StaticText

    ColumnLayout {
        id: presetInfoColumn
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space6

        GridLayout {
            Layout.fillWidth: true
            columns: root.metricColumns
            columnSpacing: theme.space12
            rowSpacing: theme.space6

            Repeater {
                model: root.metricItems

                delegate: ColumnLayout {
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.minimumWidth: 62
                    spacing: 1

                    Label {
                        Layout.fillWidth: true
                        text: modelData.label
                        color: theme.textFaint
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 7
                        elide: Text.ElideNone
                    }

                    Label {
                        Layout.fillWidth: true
                        text: modelData.value
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 8
                        elide: Text.ElideNone
                        ToolTip.visible: valueHover.hovered && contentWidth > width + 1
                        ToolTip.delay: 350
                        ToolTip.timeout: 3600
                        ToolTip.text: modelData.label + ": " + modelData.value

                        HoverHandler { id: valueHover }
                    }
                }
            }
        }

        Label {
            id: descriptionLabel
            Layout.fillWidth: true
            text: root.description
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 10
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 4
            elide: Text.ElideNone
            ToolTip.visible: descriptionHover.hovered && contentHeight > height + 1
            ToolTip.delay: 350
            ToolTip.timeout: 3600
            ToolTip.text: root.description

            HoverHandler { id: descriptionHover }
        }
    }
}
