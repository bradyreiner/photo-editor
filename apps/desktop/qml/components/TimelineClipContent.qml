import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string title: "Clip"
    property string metadata: ""
    property var effects: []
    property real horizontalInset: 0
    property bool selected: false
    readonly property string timelineClipContentContract: "timeline-clip-content:v1|title-fit:true|metadata-fit:true|effects-fit:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-fit:true|metadata-fit:true|effects-fit:true|tooltip-full:true|accessibility-full:true"

    Theme { id: theme }

    clip: true
    Accessible.name: title
    Accessible.description: metadata
    Accessible.role: Accessible.StaticText

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space8 + root.horizontalInset
        anchors.rightMargin: theme.space8 + root.horizontalInset
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6
        spacing: 2

        Label {
            id: titleLabel
            Layout.fillWidth: true
            text: root.title
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: 10
            font.weight: root.selected ? Font.DemiBold : Font.Medium
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 7
            elide: Text.ElideNone
            ToolTip.visible: titleHover.hovered && contentWidth > width + 1
            ToolTip.delay: 350
            ToolTip.timeout: 3600
            ToolTip.text: root.title

            HoverHandler { id: titleHover }
        }

        Label {
            id: metadataLabel
            Layout.fillWidth: true
            text: root.metadata
            color: theme.textMuted
            font.family: theme.fontMono
            font.pixelSize: 9
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 7
            elide: Text.ElideNone
            ToolTip.visible: metadataHover.hovered && contentWidth > width + 1
            ToolTip.delay: 350
            ToolTip.timeout: 3600
            ToolTip.text: root.metadata

            HoverHandler { id: metadataHover }
        }

        Flow {
            Layout.fillWidth: true
            spacing: theme.space4
            visible: root.effects && root.effects.length > 0
            clip: true

            Repeater {
                model: root.effects ? root.effects : []

                delegate: Rectangle {
                    id: effectChip
                    required property var modelData

                    readonly property string effectName: String(modelData || "")

                    width: Math.min(effectChipLabel.implicitWidth + 10,
                                    Math.max(38, root.width - root.horizontalInset * 2 - theme.space16))
                    height: effectChipLabel.implicitHeight + 4
                    radius: theme.radius4
                    color: theme.alpha(theme.ink950, 0.74)
                    border.width: theme.chromeBorderWidth
                    border.color: theme.alpha(theme.lineStrong, 0.34)
                    Accessible.name: effectName
                    Accessible.role: Accessible.StaticText
                    ToolTip.visible: effectHover.hovered && effectChipLabel.contentWidth > effectChipLabel.width + 1
                    ToolTip.delay: 350
                    ToolTip.timeout: 3600
                    ToolTip.text: effectName

                    HoverHandler { id: effectHover }

                    Label {
                        id: effectChipLabel
                        anchors.centerIn: parent
                        width: parent.width - 10
                        text: effectChip.effectName
                        color: theme.textMain
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 6
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideNone
                    }
                }
            }
        }
    }
}
