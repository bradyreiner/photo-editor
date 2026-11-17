import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string format: ""
    property string bitDepth: ""
    property string maxEdge: ""
    property string quality: ""
    property string description: ""
    property bool showDescription: true
    property bool compactLine: false
    readonly property int metricColumns: width < theme.scaled(420) ? 2 : 4
    readonly property var metricItems: [
        { label: "Format", value: format },
        { label: "Depth", value: bitDepth },
        { label: "Max Size", value: maxEdge },
        { label: "Quality", value: quality }
    ]
    readonly property string compactSummaryText: [format, bitDepth, maxEdge, quality]
            .map(function(value) { return String(value || "").trim() })
            .filter(function(value) { return value.length > 0 })
            .join("  ·  ")
    readonly property string summaryAccessibleText: "Export preset. " +
            root.metricItems.map(function(item) {
                return item.label + " " + item.value
            }).join(". ") + ". " + root.description
    readonly property string exportPresetSummaryContract: "export-preset-summary:v2|compact-line:" + String(compactLine) + "|metric-fit:true|description-wrap:true|adaptive-grid:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v2|compact-line:true|ellipsis:none|metric-fit:true|description-wrap:true|tooltip-full:true|accessibility-full:true"

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: presetInfoColumn.implicitHeight + theme.space16
    radius: 0
    color: "transparent"
    border.width: 0
    border.color: theme.alpha(theme.lineSoft, 0.5)
    clip: true
    Accessible.name: root.summaryAccessibleText
    Accessible.role: Accessible.StaticText
    HoverHandler { id: compactSummaryHover }
    AppToolTip {
        targetItem: root
        visible: root.compactLine && compactSummaryHover.hovered
        text: root.compactSummaryText + (root.description.length > 0 ? "\n" + root.description : "")
    }

    ColumnLayout {
        id: presetInfoColumn
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space6

        Label {
            objectName: "exportPresetSummaryLine"
            Layout.fillWidth: true
            visible: root.compactLine
            text: root.compactSummaryText
            color: theme.textMain
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: Font.Medium
            wrapMode: Text.NoWrap
            elide: Text.ElideNone
            Accessible.name: root.summaryAccessibleText
        }

        GridLayout {
            Layout.fillWidth: true
            visible: !root.compactLine
            columns: root.metricColumns
            columnSpacing: theme.space12
            rowSpacing: theme.space6

            Repeater {
                model: root.metricItems

                delegate: ColumnLayout {
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.minimumWidth: theme.scaled(72)
                    spacing: 1

                    Label {
                        Layout.fillWidth: true
                        text: modelData.label
                        color: theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeMicro
                        fontSizeMode: Text.FixedSize
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }

                    Label {
                        id: popupTooltipAnchor1
                        Layout.fillWidth: true
                        text: modelData.value
                        color: theme.textMain
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeLabel
                        font.weight: Font.Medium
                        fontSizeMode: Text.FixedSize
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                        AppToolTip {
                            id: sharedTooltip2
                            targetItem: popupTooltipAnchor1
                            visible: popupTooltipAnchor1.visible && (valueHover.hovered && popupTooltipAnchor1.contentWidth > popupTooltipAnchor1.width + 1)
                            delay: 350
                            timeout: 3600
                            text: modelData.label + ": " + modelData.value
                        }
                        HoverHandler { id: valueHover }
                    }
                }
            }
        }

        Label {
            id: descriptionLabel
            Layout.fillWidth: true
            visible: root.showDescription && !root.compactLine
            text: root.description
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeMicro
            fontSizeMode: Text.FixedSize
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 4
            elide: Text.ElideNone
            AppToolTip {
                id: sharedTooltip3
                targetItem: descriptionLabel
                visible: descriptionLabel.visible && (descriptionHover.hovered && descriptionLabel.contentHeight > descriptionLabel.height + 1)
                delay: 350
                timeout: 3600
                text: root.description
            }
            HoverHandler { id: descriptionHover }
        }
    }
}
