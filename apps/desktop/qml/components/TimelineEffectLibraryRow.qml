import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var modelData
    property string searchText: ""
    property bool actionEnabled: false
    readonly property string effectName: String(modelData.name || "")
    readonly property string effectCategory: String(modelData.category || "")
    readonly property string effectDescription: String(modelData.description || "")
    readonly property bool matchesSearch: searchText.length === 0
                                          || effectName.toLowerCase().indexOf(searchText.toLowerCase()) >= 0
                                          || effectCategory.toLowerCase().indexOf(searchText.toLowerCase()) >= 0
    readonly property string timelineEffectLibraryRowContract: "timeline-effect-library-row:v1|drag:true|add:true|search-filter:true|keyboard:true|hover:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|description-wrap:true|tooltip-full:true|accessibility-full:true"

    signal addRequested(string effectName)

    Theme { id: theme }

    visible: matchesSearch
    width: ListView.view ? ListView.view.width : 280
    height: visible ? 62 : 0
    radius: theme.radius8
    color: effectHover.hovered || activeFocus
           ? theme.alpha(theme.ink850, 0.96)
           : theme.alpha(theme.ink900, 0.88)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.9)
                  : effectHover.hovered
                    ? theme.alpha(theme.accentWarm400, 0.34)
                    : theme.alpha(theme.lineSoft, 0.34)
    clip: true
    activeFocusOnTab: true
    Accessible.name: effectName
    Accessible.description: effectCategory + ". " + effectDescription
    Accessible.role: Accessible.ListItem
    Keys.onReturnPressed: if (actionEnabled) root.addRequested(root.effectName)
    Keys.onEnterPressed: if (actionEnabled) root.addRequested(root.effectName)
    Keys.onSpacePressed: if (actionEnabled) root.addRequested(root.effectName)

    property string libraryType: "effect"
    property string libraryName: effectName
    property real libraryDurationSeconds: 0.35
    Drag.active: effectDrag.active
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["effect"]

    ToolTip.visible: effectHover.hovered && titleLabel.contentHeight > titleLabel.height + 1
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: effectName + (effectCategory.length > 0 ? " - " + effectCategory : "")

    HoverHandler { id: effectHover }

    RowLayout {
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: root.effectName + (root.effectCategory.length > 0 ? "  •  " + root.effectCategory : "")
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.Medium
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: root.effectDescription
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 9
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }

        ActionButton {
            text: "Add"
            compact: true
            enabled: root.actionEnabled
            tooltipText: root.actionEnabled ? "Add this effect to the selected clip." : "Select a clip before adding effects."
            onClicked: root.addRequested(root.effectName)
        }
    }

    DragHandler {
        id: effectDrag
    }
}
