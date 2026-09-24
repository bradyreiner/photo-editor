import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string projectName: ""
    property string summary: ""
    property int membershipCount: 0
    property int selectedCount: 0
    readonly property bool fullyApplied: membershipCount === selectedCount && selectedCount > 0
    readonly property string actionLabel: fullyApplied ? "Remove" : (membershipCount > 0 ? "Add Missing" : "Add")
    readonly property int stateEdgeWidth: 3
    readonly property int actionZoneWidth: 112
    readonly property string projectMembershipRowContract: "project-membership-row:v1|name-wrap:true|summary-wrap:true|action-state:true|keyboard:true|hover:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|name-wrap:true|summary-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string rowAnatomyContract: "row-anatomy:v1|state-edge:left|content:name-summary-wrap|action-zone:fixed-112|selection:left-edge|hover:neutral|focus:ring"

    signal toggleRequested(string projectName)

    Theme { id: theme }

    Layout.fillWidth: true
    radius: theme.radius12
    color: rowHover.hovered || activeFocus
           ? theme.alpha(theme.ink850, fullyApplied ? 0.74 : 0.62)
           : theme.alpha(theme.ink950, fullyApplied ? 0.64 : 0.42)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.9)
                  : fullyApplied
                    ? theme.alpha(theme.accentCool400, 0.42)
                    : theme.alpha(theme.lineSoft, rowHover.hovered ? 0.34 : 0.24)
    implicitHeight: projectRow.implicitHeight + theme.space14
    clip: true
    activeFocusOnTab: true
    Accessible.name: projectName
    Accessible.description: summary
    Accessible.role: Accessible.ListItem
    Keys.onReturnPressed: root.toggleRequested(root.projectName)
    Keys.onEnterPressed: root.toggleRequested(root.projectName)
    Keys.onSpacePressed: root.toggleRequested(root.projectName)

    HoverHandler { id: rowHover }

    RowLayout {
        id: projectRow
        anchors.fill: parent
        anchors.margins: theme.space10
        spacing: theme.space10

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: root.stateEdgeWidth
            Layout.fillHeight: true
            radius: 2
            color: root.fullyApplied
                   ? theme.accentCool400
                   : theme.alpha(theme.lineSoft, rowHover.hovered ? 0.34 : 0.14)
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                id: projectNameLabel
                Layout.fillWidth: true
                text: root.projectName
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                ToolTip.visible: rowHover.hovered && projectNameLabel.contentHeight > projectNameLabel.height + 1
                ToolTip.delay: 350
                ToolTip.timeout: 3600
                ToolTip.text: root.projectName
            }

            Label {
                id: summaryLabel
                Layout.fillWidth: true
                text: root.summary
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                ToolTip.visible: rowHover.hovered && summaryLabel.contentHeight > summaryLabel.height + 1
                ToolTip.delay: 350
                ToolTip.timeout: 3600
                ToolTip.text: root.summary
            }
        }

        ActionButton {
            text: root.actionLabel
            compact: true
            active: root.fullyApplied
            minimumReadableWidth: root.actionZoneWidth
            tooltipText: root.actionLabel + " project membership."
            Layout.minimumWidth: root.actionZoneWidth
            Layout.preferredWidth: root.actionZoneWidth
            onClicked: root.toggleRequested(root.projectName)
        }
    }
}
