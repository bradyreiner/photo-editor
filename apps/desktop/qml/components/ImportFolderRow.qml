import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string fileName: "Folder"
    property string fileUrl: ""
    property bool selected: false
    readonly property string importFolderRowContract: "import-folder-row:v2|active-state:true|keyboard:true|hover:true|readable-text:true|no-elide:true|selection:surface-text"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    signal activated(string fileUrl)

    Theme { id: theme }

    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: Math.max(theme.controlHeightDense, folderContent.implicitHeight + theme.space8)
    radius: theme.radiusControl
    color: selected
           ? theme.editorSelectionFill
           : (folderHover.hovered ? theme.editorControlHover : "transparent")
    border.width: activeFocus ? theme.focusBorderWidth : (selected ? theme.hairlineWidth : 0)
    border.color: activeFocus
                  ? theme.focusRing
                  : selected
                    ? theme.alpha(theme.editorSelectionEdge, 0.72)
                    : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: fileName
    Accessible.description: selected ? "Currently active folder" : "Open folder"
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: root.activated(root.fileUrl)
    Keys.onEnterPressed: root.activated(root.fileUrl)
    Keys.onSpacePressed: root.activated(root.fileUrl)

    HoverHandler {
        id: folderHover
        cursorShape: Qt.PointingHandCursor
    }

    RowLayout {
        id: folderContent
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        anchors.topMargin: theme.space4
        anchors.bottomMargin: theme.space4
        spacing: theme.space8

        Label {
            id: folderLabel
            Layout.fillWidth: true
            text: root.fileName
            color: root.selected ? theme.interaction300 : (folderHover.hovered ? theme.textMain : theme.textMuted)
            font.family: theme.fontUi
            font.pixelSize: theme.typeLabel
            font.weight: theme.weightControl
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
            ToolTip.visible: folderHover.hovered && (folderLabel.truncated || folderLabel.contentHeight > folderLabel.height + 1)
            ToolTip.delay: 350
            ToolTip.timeout: 3600
            ToolTip.text: root.fileName
        }

        AppIcon {
            name: "chevron-right"
            color: theme.textFaint
            implicitWidth: 12
            implicitHeight: 12
            Layout.alignment: Qt.AlignVCenter
            opacity: folderHover.hovered || root.selected ? 1.0 : 0.65
        }
    }

    TapHandler {
        onTapped: root.activated(root.fileUrl)
    }
}
