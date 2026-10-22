import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property int rowIndex: -1
    property string label: ""
    property string shortcutHint: ""
    property string category: ""
    property bool itemEnabled: true
    property bool current: false
    readonly property string commandPaletteRowContract: "command-palette-row:v1|current-state:true|disabled-state:true|hover:true|keyboard-compatible:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|category-fit:true|shortcut-fit:true|tooltip-full:true|accessibility-full:true"

    signal selected(int rowIndex)
    signal activated(int rowIndex)

    Theme { id: theme }

    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: 36
    color: current
           ? theme.alpha(theme.accentWarm400, 0.14)
           : rowHover.hovered
             ? theme.alpha(theme.polar, 0.06)
             : "transparent"
    opacity: itemEnabled ? 1 : 0.58
    clip: true
    Accessible.name: label
    Accessible.description: category + (shortcutHint.length > 0 ? ", shortcut " + shortcutHint : "")
    Accessible.role: Accessible.Button

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space14
        anchors.rightMargin: theme.space14
        spacing: theme.space8

        Label {
            id: popupTooltipAnchor1
            visible: root.category.length > 0
            Layout.maximumWidth: 108
            text: root.category
            color: theme.textFaint
            font.family: theme.fontUi
            font.pixelSize: 11
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
            AppToolTip {
                id: sharedTooltip2
                targetItem: popupTooltipAnchor1
                visible: popupTooltipAnchor1.visible && (rowHover.hovered && popupTooltipAnchor1.contentWidth > popupTooltipAnchor1.width + 1)
                delay: 350
                timeout: 3600
                text: root.category
            }
        }

        Label {
            id: commandLabel
            Layout.fillWidth: true
            text: root.label
            color: root.itemEnabled ? theme.textMain : theme.textFaint
            font.family: theme.fontUi
            font.pixelSize: 13
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 10
            elide: Text.ElideNone
            AppToolTip {
                id: sharedTooltip3
                targetItem: commandLabel
                visible: commandLabel.visible && (rowHover.hovered && commandLabel.contentWidth > commandLabel.width + 1)
                delay: 350
                timeout: 3600
                text: root.label
            }
        }

        Label {
            visible: root.shortcutHint.length > 0
            Layout.maximumWidth: 112
            text: root.shortcutHint
            color: theme.textFaint
            font.family: theme.fontMono
            font.pixelSize: 11
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
        }
    }

    HoverHandler {
        id: rowHover
        cursorShape: root.itemEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onHoveredChanged: {
            if (hovered)
                root.selected(root.rowIndex)
        }
    }

    TapHandler {
        onTapped: {
            root.selected(root.rowIndex)
            if (root.itemEnabled)
                root.activated(root.rowIndex)
        }
    }
}
