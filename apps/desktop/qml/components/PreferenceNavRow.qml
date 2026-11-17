import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
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
    hoverEnabled: true
    background: CommandButtonSurface {
        selected: root.selected
        hovered: rowHover.hovered
        focused: root.visualFocus
        interactiveEnabled: root.enabled
        fillColor: theme.commandFill("secondary", root.selected, rowHover.hovered, false, root.enabled, true)
    }
    activeFocusOnTab: true
    Accessible.name: title
    Accessible.description: subtitle
    Accessible.role: Accessible.Button
    Accessible.checked: root.selected
    Keys.onReturnPressed: if (root.enabled) root.activated(root.pageIndex)
    Keys.onEnterPressed: if (root.enabled) root.activated(root.pageIndex)
    onClicked: root.activated(root.pageIndex)

    HoverHandler {
        id: rowHover
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: RowLayout {
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
                objectName: "sharedCommandLabel"
                Layout.fillWidth: true
                text: root.title
                color: root.selected ? theme.interaction300 : theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: theme.weightControl
                fontSizeMode: Text.FixedSize
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

}
