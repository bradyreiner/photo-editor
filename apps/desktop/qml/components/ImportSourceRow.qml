import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string label: "Source"
    property string subtitle: ""
    property bool showSubtitle: false
    property string path: ""
    property string sourceUrl: ""
    property bool selected: false
    readonly property string importSourceRowContract: "import-source-row:v2|active-state:true|hover:true|keyboard:true|readable-text:true|path:tooltip-accessible"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|label-wrap:true|subtitle-wrap:true|path:not-inline|tooltip-full:true|accessibility-full:true"
    readonly property string rowAnatomyContract: "row-anatomy:v2|content:title-detail-wrap|selection:surface-text|hover:neutral|focus:ring|activation:path"

    signal activated(string path, string sourceUrl, string label)

    Theme { id: theme }

    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: Math.max(theme.controlHeightDense + theme.space6, sourceRow.implicitHeight + theme.space12)
    radius: theme.radiusControl
    color: !enabled
           ? theme.alpha(theme.ink850, 0.4)
           : selected
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
                            ? "Currently active import source. " + subtitle + (path.length > 0 ? ". " + path : "")
                            : subtitle + (path.length > 0 ? ". " + path : "")
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: if (root.enabled) root.activated(root.path, root.sourceUrl, root.label)
    Keys.onEnterPressed: if (root.enabled) root.activated(root.path, root.sourceUrl, root.label)
    Keys.onSpacePressed: if (root.enabled) root.activated(root.path, root.sourceUrl, root.label)
    opacity: enabled ? 1.0 : 0.62

    Behavior on color {
        ColorAnimation { duration: theme.durationFast }
    }

    Behavior on border.color {
        ColorAnimation { duration: theme.durationFast }
    }

    HoverHandler {
        id: sourceHover
        enabled: root.enabled
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    AppToolTip {
        targetItem: root
        visible: root.enabled && sourceHover.hovered && root.path.length > 0
        delay: 450
        timeout: 4200
        text: root.path
    }

    RowLayout {
        id: sourceRow
        anchors.fill: parent
        anchors.leftMargin: theme.space10
        anchors.rightMargin: theme.space10
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6
        spacing: theme.space8

        AppIcon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: theme.space2
            name: "folder"
            implicitWidth: theme.scaled(16)
            implicitHeight: theme.scaled(16)
            color: root.selected ? theme.textStrong : theme.textMuted
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
                font.pixelSize: theme.typeLabel
                font.weight: root.selected ? theme.weightSection : theme.weightControl
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                AppToolTip {
                    id: sharedTooltip1
                    targetItem: titleLabel
                    visible: titleLabel.visible && (sourceHover.hovered && (titleLabel.truncated || titleLabel.contentHeight > titleLabel.height + 1))
                    delay: 350
                    timeout: 3600
                    text: root.label
                }
            }

            Label {
                id: subtitleLabel
                visible: root.showSubtitle && root.subtitle.length > 0
                Layout.fillWidth: true
                text: root.subtitle
                color: root.enabled ? (root.selected ? theme.textMain : theme.textMuted) : theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                AppToolTip {
                    id: sharedTooltip2
                    targetItem: subtitleLabel
                    visible: subtitleLabel.visible && (sourceHover.hovered && (subtitleLabel.truncated || subtitleLabel.contentHeight > subtitleLabel.height + 1))
                    delay: 350
                    timeout: 3600
                    text: root.subtitle
                }
            }

        }
    }

    TapHandler {
        enabled: root.enabled
        onTapped: {
            root.forceActiveFocus()
            root.activated(root.path, root.sourceUrl, root.label)
        }
    }
}
