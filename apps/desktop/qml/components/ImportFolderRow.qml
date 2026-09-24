import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string fileName: "Folder"
    property string fileUrl: ""
    property bool selected: false
    readonly property string importFolderRowContract: "import-folder-row:v1|active-state:true|keyboard:true|hover:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    signal activated(string fileUrl)

    Theme { id: theme }

    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: 36
    radius: theme.radius8
    color: selected
           ? theme.alpha(theme.accentCool500, 0.1)
           : (folderHover.hovered || activeFocus ? theme.alpha(theme.ink800, 0.84) : theme.alpha(theme.ink950, 0.16))
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus
                  ? theme.alpha(theme.focusRing, 0.88)
                  : selected
                    ? theme.alpha(theme.accentWarm400, 0.44)
                    : (folderHover.hovered ? theme.alpha(theme.lineSoft, 0.26) : theme.alpha(theme.lineSoft, 0.1))
    clip: true
    activeFocusOnTab: true
    Accessible.name: fileName
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: root.activated(root.fileUrl)
    Keys.onEnterPressed: root.activated(root.fileUrl)
    Keys.onSpacePressed: root.activated(root.fileUrl)

    HoverHandler {
        id: folderHover
        cursorShape: Qt.PointingHandCursor
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space10
        anchors.rightMargin: theme.space10
        spacing: theme.space8

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 6
            implicitHeight: 6
            radius: 3
            color: root.selected
                   ? theme.accentWarm400
                   : theme.alpha(theme.accentCool400, folderHover.hovered ? 0.82 : 0.46)
        }

        Label {
            id: folderLabel
            Layout.fillWidth: true
            text: root.fileName
            color: root.selected ? theme.textStrong : theme.textMain
            font.family: theme.fontUi
            font.pixelSize: 11
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            elide: Text.ElideNone
            ToolTip.visible: folderHover.hovered && folderLabel.contentWidth > folderLabel.width + 1
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
        }
    }

    TapHandler {
        onTapped: root.activated(root.fileUrl)
    }
}
