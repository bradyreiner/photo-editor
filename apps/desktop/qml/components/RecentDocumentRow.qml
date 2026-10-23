import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string title: "Untitled"
    property string detail: ""
    property string iconName: ""
    property color accent: theme.accentCool400
    property bool selected: false
    property bool actionable: true
    readonly property int stateEdgeWidth: 3
    readonly property int iconZoneWidth: 18
    readonly property string recentDocumentRowContract: "recent-document-row:v1|title-wrap:true|detail-wrap:true|hover:true|keyboard:true|readable-text:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|detail-wrap:true|tooltip-full:true"
    readonly property string rowAnatomyContract: "row-anatomy:v1|state-edge:left|icon-zone:fixed-18|content:title-detail-wrap|selection:left-edge|hover:neutral|focus:ring"

    signal activated()

    Theme { id: theme }

    implicitHeight: Math.max(64, recentContent.implicitHeight + theme.space12)
    radius: theme.radius8
    color: selected
           ? theme.alpha(accent, 0.16)
           : rowHover.hovered
             ? theme.alpha(accent, 0.12)
             : theme.alpha(theme.ink900, 0.62)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.9)
                  : selected || rowHover.hovered
                    ? theme.alpha(accent, 0.42)
                    : theme.alpha(theme.lineSoft, 0.24)
    clip: true
    focus: false
    activeFocusOnTab: actionable
    Accessible.name: title
    Accessible.description: detail
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: if (actionable) root.activated()
    Keys.onEnterPressed: if (actionable) root.activated()
    Keys.onSpacePressed: if (actionable) root.activated()

    HoverHandler { id: rowHover; enabled: root.actionable }

    RowLayout {
        id: recentContent
        anchors.fill: parent
        anchors.leftMargin: theme.space12
        anchors.rightMargin: theme.space12
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6
        spacing: theme.space8

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: root.stateEdgeWidth
            Layout.fillHeight: true
            radius: 2
            color: root.selected
                   ? root.accent
                   : theme.alpha(theme.lineSoft, rowHover.hovered ? 0.32 : 0.12)
        }

        AppIcon {
            visible: root.iconName.length > 0
            Layout.minimumWidth: root.iconZoneWidth
            Layout.preferredWidth: root.iconZoneWidth
            Layout.preferredHeight: 14
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
            name: root.iconName
            color: root.selected ? root.accent : theme.textMuted
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: root.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 12
                font.weight: Font.DemiBold
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                AppToolTip {
                    id: sharedTooltip1
                    targetItem: titleLabel
                    visible: titleLabel.visible && (titleHover.hovered && titleLabel.contentWidth > titleLabel.width + 1)
                    delay: 350
                    timeout: 3600
                    text: root.title
                }
                HoverHandler { id: titleHover }
            }

            Label {
                id: detailLabel
                Layout.fillWidth: true
                visible: root.detail.length > 0
                text: root.detail
                color: theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: 9
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                AppToolTip {
                    id: sharedTooltip2
                    targetItem: detailLabel
                    visible: detailLabel.visible && (detailHover.hovered && detailLabel.contentWidth > detailLabel.width + 1)
                    delay: 350
                    timeout: 3600
                    text: root.detail
                }
                HoverHandler { id: detailHover }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.actionable
        hoverEnabled: false
        cursorShape: Qt.PointingHandCursor
        onClicked: root.activated()
    }
}
