import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string label: "Source"
    property string subtitle: ""
    property string path: ""
    property string sourceUrl: ""
    property bool selected: false
    readonly property int stateEdgeWidth: 3
    readonly property int statusZoneMinWidth: 72
    readonly property string importSourceRowContract: "import-source-row:v1|active-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-wrap:true|subtitle-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string rowAnatomyContract: "row-anatomy:v1|state-edge:left|content:title-detail-wrap|status-zone:fixed-min-72|selection:left-edge|hover:neutral|focus:ring"

    signal activated(string path, string sourceUrl, string label)

    Theme { id: theme }

    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: Math.max(50, sourceRow.implicitHeight + theme.space12)
    radius: theme.radius8
    color: selected
           ? theme.alpha(theme.accentCool500, 0.12)
           : (sourceHover.hovered || activeFocus ? theme.alpha(theme.ink800, 0.88) : theme.alpha(theme.ink900, 0.82))
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.88)
                  : selected
                    ? theme.alpha(theme.accentWarm400, 0.64)
                    : (sourceHover.hovered ? theme.alpha(theme.lineSoft, 0.34) : theme.alpha(theme.lineSoft, 0.18))
    clip: true
    activeFocusOnTab: true
    Accessible.name: label
    Accessible.description: subtitle
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: root.activated(root.path, root.sourceUrl, root.label)
    Keys.onEnterPressed: root.activated(root.path, root.sourceUrl, root.label)
    Keys.onSpacePressed: root.activated(root.path, root.sourceUrl, root.label)

    Behavior on color {
        ColorAnimation { duration: theme.durationFast }
    }

    Behavior on border.color {
        ColorAnimation { duration: theme.durationFast }
    }

    HoverHandler {
        id: sourceHover
        cursorShape: Qt.PointingHandCursor
    }

    RowLayout {
        id: sourceRow
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space8

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 3
            Layout.fillHeight: true
            radius: 2
            color: root.selected
                   ? theme.accentWarm400
                   : theme.alpha(theme.lineSoft, sourceHover.hovered ? 0.34 : 0.14)
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: root.label
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 12
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 1
                elide: Text.ElideNone
                ToolTip.visible: sourceHover.hovered && titleLabel.contentHeight > titleLabel.height + 1
                ToolTip.delay: 350
                ToolTip.timeout: 3600
                ToolTip.text: root.label
            }

            Label {
                id: subtitleLabel
                Layout.fillWidth: true
                text: root.subtitle
                color: root.selected ? theme.textMain : theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 9
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 1
                elide: Text.ElideNone
                ToolTip.visible: sourceHover.hovered && subtitleLabel.contentHeight > subtitleLabel.height + 1
                ToolTip.delay: 350
                ToolTip.timeout: 3600
                ToolTip.text: root.subtitle
            }
        }

        StatusChip {
            visible: root.selected || sourceHover.hovered
            text: root.selected ? "Active" : "Open"
            tone: root.selected ? "accent" : "neutral"
            compact: true
            tooltipText: root.selected ? "This import source is active." : "Open this import source."
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: root.statusZoneMinWidth
            Layout.preferredWidth: Math.max(root.statusZoneMinWidth, implicitWidth)
            Layout.preferredHeight: implicitHeight
        }
    }

    TapHandler {
        onTapped: root.activated(root.path, root.sourceUrl, root.label)
    }
}
