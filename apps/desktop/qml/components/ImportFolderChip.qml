import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property string fileName
    required property string fileUrl
    readonly property string importFolderChipContract: "import-folder-chip:v1|readable-text:true|keyboard:true|hover:true|tooltip-full:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    signal activated(string fileUrl)

    Theme { id: theme }

    implicitWidth: Math.min(theme.scaled(252), Math.max(theme.scaled(108), folderLabel.implicitWidth + theme.space20))
    implicitHeight: 34
    width: implicitWidth
    height: implicitHeight
    radius: theme.radiusControl
    color: !enabled
           ? theme.alpha(theme.ink850, 0.42)
           : chipHover.hovered
             ? theme.editorControlHover
             : theme.alpha(theme.editorControlIdle, 0.74)
    border.width: activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
    border.color: activeFocus
                  ? theme.focusRing
                  : theme.alpha(theme.lineSoft, 0.46)
    clip: true
    activeFocusOnTab: true
    Accessible.name: fileName
    Accessible.description: "Open folder " + fileUrl
    Accessible.role: Accessible.Button
    Keys.onReturnPressed: if (root.enabled) root.activated(root.fileUrl)
    Keys.onEnterPressed: if (root.enabled) root.activated(root.fileUrl)
    Keys.onSpacePressed: if (root.enabled) root.activated(root.fileUrl)
    opacity: enabled ? 1.0 : 0.62

    Behavior on color {
        ColorAnimation { duration: theme.durationFast }
    }

    HoverHandler {
        id: chipHover
        enabled: root.enabled
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    Label {
        id: folderLabel
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        text: root.fileName
        color: root.enabled ? theme.textMain : theme.textFaint
        font.family: theme.fontUi
        font.pixelSize: theme.typeMicro
        font.weight: theme.weightControl
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: theme.scaled(10)
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideNone
        AppToolTip {
            id: sharedTooltip1
            targetItem: folderLabel
            visible: folderLabel.visible && (chipHover.hovered && folderLabel.contentWidth > folderLabel.width + 1)
            delay: 350
            timeout: 3600
            text: root.fileName
        }
    }

    TapHandler {
        enabled: root.enabled
        onTapped: {
            root.forceActiveFocus()
            root.activated(root.fileUrl)
        }
    }
}
