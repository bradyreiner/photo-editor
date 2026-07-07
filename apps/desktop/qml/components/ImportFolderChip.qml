import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string fileName
    required property string fileUrl
    readonly property string importFolderChipContract: "import-folder-chip:v1|readable-text:true|keyboard:true|hover:true|tooltip-full:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    signal activated(string fileUrl)

    Theme { id: theme }

    implicitWidth: Math.min(220, Math.max(108, folderLabel.implicitWidth + theme.space20))
    implicitHeight: 34
    width: implicitWidth
    height: implicitHeight
    radius: theme.radius8
    color: chipHover.hovered || activeFocus
           ? theme.alpha(theme.accentCool400, 0.14)
           : theme.alpha(theme.ink900, 0.92)
    border.width: activeFocus ? 2 : 0
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.88)
                  : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: fileName
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: root.activated(root.fileUrl)
    Keys.onEnterPressed: root.activated(root.fileUrl)
    Keys.onSpacePressed: root.activated(root.fileUrl)

    HoverHandler {
        id: chipHover
        cursorShape: Qt.PointingHandCursor
    }

    Label {
        id: folderLabel
        anchors.fill: parent
        anchors.leftMargin: theme.space10
        anchors.rightMargin: theme.space10
        text: root.fileName
        color: theme.textMain
        font.family: theme.fontUi
        font.pixelSize: 10
        font.weight: Font.Medium
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 8
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideNone
        ToolTip.visible: chipHover.hovered && folderLabel.contentWidth > folderLabel.width + 1
        ToolTip.delay: 350
        ToolTip.timeout: 3600
        ToolTip.text: root.fileName
    }

    TapHandler {
        onTapped: root.activated(root.fileUrl)
    }
}
