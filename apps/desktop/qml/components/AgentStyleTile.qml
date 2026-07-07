import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var styleData: ({})
    property bool selected: false
    property bool tileEnabled: true
    readonly property string styleId: String(styleData.id || "")
    readonly property string styleName: String(styleData.name || styleData.id || "Style")
    readonly property int minimumReadableWidth: 240
    readonly property string agentStyleTileContract: "agent-style-tile:v2|selected-state:true|keyboard:true|hover:true|style-name-wrap:true|responsive-columns:true|minimum-readable-width:240|readable-text:true|no-elide:true|no-clip:true"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|style-name-wrap:true|max-lines:4|horizontal-fit:true|tooltip-full:true|accessibility-full:true|no-clip:true"

    signal selectedRequested(string styleId)

    Theme { id: theme }

    Layout.fillWidth: true
    Layout.minimumWidth: Math.min(root.minimumReadableWidth, parent ? parent.width : root.minimumReadableWidth)
    implicitWidth: root.minimumReadableWidth
    implicitHeight: Math.max(44, tileContent.implicitHeight + theme.space12)
    radius: theme.radiusControl
    color: !tileEnabled
           ? theme.alpha(theme.glassBase, 0.24)
           : selected
             ? theme.alpha(theme.focusRing, 0.12)
             : tileHover.hovered
               ? theme.alpha(theme.glassLift, 0.34)
               : theme.alpha(theme.glassBase, 0.34)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.9)
                  : selected
                    ? theme.alpha(theme.focusRing, 0.52)
                    : theme.alpha(theme.glassEdge, 0.22)
    opacity: tileEnabled ? 1 : 0.58
    clip: false
    activeFocusOnTab: true
    Accessible.name: styleName
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: {
        if (root.tileEnabled)
            root.selectedRequested(root.styleId)
    }
    Keys.onEnterPressed: {
        if (root.tileEnabled)
            root.selectedRequested(root.styleId)
    }
    Keys.onSpacePressed: {
        if (root.tileEnabled)
            root.selectedRequested(root.styleId)
    }

    HoverHandler {
        id: tileHover
        cursorShape: root.tileEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
    }

    RowLayout {
        id: tileContent
        anchors.fill: parent
        anchors.leftMargin: theme.space8 + 1
        anchors.rightMargin: theme.space8 + 1
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6
        spacing: theme.space6 + 1

        AppIcon {
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
            name: root.selected ? "check" : "menu"
            color: root.selected ? theme.focusRing : theme.textMuted
        }

        Text {
            id: styleLabel
            Layout.fillWidth: true
            text: root.styleName
            color: root.selected ? theme.textStrong : theme.textMain
            font.family: theme.fontUi
            font.pixelSize: 11
            font.weight: root.selected ? Font.DemiBold : Font.Medium
            font.letterSpacing: 0
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 4
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 9
            ToolTip.visible: tileHover.hovered && contentHeight > height + 1
            ToolTip.delay: 350
            ToolTip.timeout: 3600
            ToolTip.text: root.styleName
        }
    }

    TapHandler {
        enabled: root.tileEnabled
        onTapped: root.selectedRequested(root.styleId)
    }
}
