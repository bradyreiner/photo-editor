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
    readonly property string importSourceRowContract: "import-source-row:v1|active-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-wrap:true|subtitle-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string rowAnatomyContract: "row-anatomy:v2|content:title-detail-wrap|selection:surface-text|hover:neutral|focus:ring|activation:path"

    signal activated(string path, string sourceUrl, string label)

    Theme { id: theme }

    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: Math.max(theme.controlHeightDense + theme.space6, sourceRow.implicitHeight + theme.space8)
    radius: theme.radiusControl
    color: selected
           ? theme.editorSelectionFill
           : (sourceHover.hovered ? theme.editorControlHover : "transparent")
    border.width: activeFocus ? theme.focusBorderWidth : (selected ? theme.hairlineWidth : 0)
    border.color: activeFocus
                  ? theme.focusRing
                  : selected
                    ? theme.alpha(theme.editorSelectionEdge, 0.72)
                    : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: label
    Accessible.description: selected
                            ? (subtitle.length > 0 ? "Currently active import source. " + subtitle : "Currently active import source")
                            : subtitle
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
        anchors.leftMargin: theme.space10
        anchors.rightMargin: theme.space10
        anchors.topMargin: theme.space4
        anchors.bottomMargin: theme.space4
        spacing: theme.space8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: root.label
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeLabel
                font.weight: theme.weightControl
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                ToolTip.visible: sourceHover.hovered && (titleLabel.truncated || titleLabel.contentHeight > titleLabel.height + 1)
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
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                ToolTip.visible: sourceHover.hovered && (subtitleLabel.truncated || subtitleLabel.contentHeight > subtitleLabel.height + 1)
                ToolTip.delay: 350
                ToolTip.timeout: 3600
                ToolTip.text: root.subtitle
            }
        }

    }

    TapHandler {
        onTapped: root.activated(root.path, root.sourceUrl, root.label)
    }
}
