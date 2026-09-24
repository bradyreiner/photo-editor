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
    readonly property string preferenceNavRowContract: "preference-nav-row:v1|selected-state:true|hover:true|keyboard:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-fit:true|subtitle-wrap:true|tooltip-full:true|accessibility-full:true"

    signal activated(int pageIndex)

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: Math.max(74, rowContent.implicitHeight + theme.space18)
    radius: theme.radiusPanel
    color: selected
           ? theme.alpha(accent, 0.14)
           : (rowHover.hovered || activeFocus ? theme.alpha(theme.glassLift, 0.64) : theme.alpha(theme.glassBase, 0.38))
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.88)
                  : selected
                    ? theme.alpha(accent, 0.58)
                    : theme.alpha(theme.glassEdge, rowHover.hovered ? 0.42 : 0.22)
    clip: true
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
        anchors.margins: theme.space12
        spacing: theme.space12

        Rectangle {
            Layout.alignment: Qt.AlignTop
            implicitWidth: 6
            implicitHeight: 6
            radius: 1
            color: root.accent
            opacity: root.selected ? 1.0 : 0.34
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space4

            Label {
                id: titleLabel
                Layout.fillWidth: true
                text: root.title
                color: root.selected ? theme.textStrong : theme.textMain
                font.family: theme.fontUi
                font.pixelSize: 13
                font.weight: Font.DemiBold
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 10
                elide: Text.ElideNone
                ToolTip.visible: rowHover.hovered && contentWidth > width + 1
                ToolTip.delay: 350
                ToolTip.timeout: 3600
                ToolTip.text: root.title
            }

            Label {
                id: subtitleLabel
                Layout.fillWidth: true
                text: root.subtitle
                color: theme.textFaint
                font.family: theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
                ToolTip.visible: rowHover.hovered && contentHeight > height + 1
                ToolTip.delay: 350
                ToolTip.timeout: 3600
                ToolTip.text: root.subtitle
            }
        }
    }

    TapHandler {
        onTapped: root.activated(root.pageIndex)
    }
}
