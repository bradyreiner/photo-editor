import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property var modelData
    property string searchText: ""
    property bool actionEnabled: false
    readonly property string transitionName: String(modelData.name || "")
    readonly property string transitionDescription: String(modelData.description || "")
    readonly property real transitionDurationSeconds: transitionName === "Cut" ? 0 : 0.35
    readonly property bool matchesSearch: searchText.length === 0
                                          || transitionName.toLowerCase().indexOf(searchText.toLowerCase()) >= 0
    readonly property string timelineTransitionLibraryRowContract: "timeline-transition-library-row:v1|drag:true|in-out:true|search-filter:true|keyboard:true|hover:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|description-wrap:true|tooltip-full:true|accessibility-full:true"

    signal transitionRequested(string edge, string transitionName, real durationSeconds)

    Theme { id: theme }

    visible: matchesSearch
    width: ListView.view ? ListView.view.width : 280
    height: visible ? 60 : 0
    radius: theme.radius8
    color: transitionHover.hovered || activeFocus
           ? theme.alpha(theme.ink850, 0.96)
           : theme.alpha(theme.ink900, 0.88)
    border.width: activeFocus ? theme.focusBorderWidth : 0
    border.color: activeFocus ? theme.alpha(theme.focusRing, 0.9) : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: transitionName
    Accessible.description: transitionDescription
    Accessible.role: Accessible.ListItem
    Keys.onReturnPressed: if (actionEnabled) root.transitionRequested("in", root.transitionName, root.transitionDurationSeconds)
    Keys.onEnterPressed: if (actionEnabled) root.transitionRequested("in", root.transitionName, root.transitionDurationSeconds)
    Keys.onSpacePressed: if (actionEnabled) root.transitionRequested("in", root.transitionName, root.transitionDurationSeconds)

    property string libraryType: "transition"
    property string libraryName: transitionName
    property real libraryDurationSeconds: transitionDurationSeconds
    Drag.active: transitionDrag.active
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["transition"]

    ToolTip.visible: transitionHover.hovered && titleLabel.contentHeight > titleLabel.height + 1
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: transitionName

    HoverHandler { id: transitionHover }

    RowLayout {
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space2

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: root.transitionName
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                font.weight: Font.Medium
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: root.transitionDescription
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }

        ColumnLayout {
            spacing: theme.space4

            ActionButton {
                text: "In"
                compact: true
                enabled: root.actionEnabled
                tooltipText: root.actionEnabled ? "Assign as incoming transition." : "Select a clip before assigning transitions."
                onClicked: root.transitionRequested("in", root.transitionName, root.transitionDurationSeconds)
            }

            ActionButton {
                text: "Out"
                compact: true
                enabled: root.actionEnabled
                tooltipText: root.actionEnabled ? "Assign as outgoing transition." : "Select a clip before assigning transitions."
                onClicked: root.transitionRequested("out", root.transitionName, root.transitionDurationSeconds)
            }
        }
    }

    DragHandler {
        id: transitionDrag
    }
}
