import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property int pageIndex: -1
    property string title: ""
    property string subtitle: ""
    property color accent: theme.accentWarm400
    property bool selected: false
    property bool showSubtitle: true
    readonly property string preferenceNavRowContract: "preference-nav-row:v1|selected-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-fit:true|subtitle-wrap:true|tooltip-full:true|accessibility-full:true"

    signal activated(int pageIndex)

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: Math.max(showSubtitle ? theme.sidebarRowHeightCompact + theme.space8 : theme.sidebarRowHeightCompact,
                             rowContent.implicitHeight + theme.space10)
    radius: theme.radiusControl
    color: selected
           ? theme.editorSelectionFill
           : (rowHover.hovered || activeFocus ? theme.editorControlHover : "transparent")
    border.width: activeFocus ? theme.focusBorderWidth : (selected ? theme.hairlineWidth : 0)
    border.color: activeFocus
                  ? theme.focusRing
                  : selected
                    ? theme.alpha(theme.editorSelectionEdge, 0.72)
                    : "transparent"
    activeFocusOnTab: true
    Accessible.name: title
    Accessible.description: subtitle
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: root.activated(root.pageIndex)
    Keys.onEnterPressed: root.activated(root.pageIndex)
    Keys.onSpacePressed: root.activated(root.pageIndex)

    Behavior on color {
        ColorAnimation { duration: theme.durationFast }
    }

    Behavior on border.color {
        ColorAnimation { duration: theme.durationFast }
    }

    HoverHandler {
        id: rowHover
        cursorShape: Qt.PointingHandCursor
    }

    RowLayout {
        id: rowContent
        anchors.fill: parent
        anchors.leftMargin: theme.space10
        anchors.rightMargin: theme.space10
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6
        spacing: theme.space10

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space2

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: root.title
                color: root.selected ? theme.interaction300 : theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: root.selected ? Font.DemiBold : Font.Medium
                fontSizeMode: root.showSubtitle ? Text.HorizontalFit : Text.FixedSize
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                minimumPixelSize: 10
                elide: Text.ElideNone
                AppToolTip {
                    id: sharedTooltip1
                    targetItem: titleLabel
                    visible: titleLabel.visible && (rowHover.hovered && (!root.showSubtitle || titleLabel.contentWidth > titleLabel.width + 1))
                    delay: 350
                    timeout: 3600
                    text: root.showSubtitle ? root.title : root.subtitle
                }
            }

            Label {
                id: subtitleLabel
                visible: root.showSubtitle
                Layout.fillWidth: true
                text: root.subtitle
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                AppToolTip {
                    id: sharedTooltip2
                    targetItem: subtitleLabel
                    visible: subtitleLabel.visible && (rowHover.hovered && subtitleLabel.contentHeight > subtitleLabel.height + 1)
                    delay: 350
                    timeout: 3600
                    text: root.subtitle
                }
            }
        }
    }

    TapHandler {
        onTapped: root.activated(root.pageIndex)
    }
}
