import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

MenuItem {
    id: root

    property string iconName: ""
    property string shortcutText: ""
    property string tooltipText: ""
    property bool danger: false
    property color accent: theme.focusRing
    readonly property string appMenuItemContract: "app-menu-item:v1|hover:true|keyboard:true|disabled-state:true|shortcut-fit:true|readable-text:true|no-elide:true"

    Theme { id: theme }

    implicitWidth: Math.max(theme.menuMinWidth, menuContent.implicitWidth + theme.space20)
    implicitHeight: Math.max(theme.menuItemHeight, menuContent.implicitHeight + theme.space8)

    contentItem: RowLayout {
        id: menuContent
        spacing: theme.space8

        AppIcon {
            visible: root.iconName.length > 0
            name: root.iconName
            color: !root.enabled
                   ? theme.alpha(theme.textMuted, 0.5)
                   : root.danger
                     ? theme.error
                   : (root.highlighted ? theme.selectionAccent : theme.textMain)
            Layout.preferredWidth: theme.typeControl
            Layout.preferredHeight: theme.typeControl
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            Layout.fillWidth: true
            text: root.text
            color: !root.enabled
                   ? theme.alpha(theme.textMuted, 0.52)
                   : root.danger
                     ? theme.error
                     : theme.textMain
            font.family: theme.fontUi
            font.pixelSize: theme.typeBody
            font.weight: theme.weightControl
            font.letterSpacing: 0
            verticalAlignment: Text.AlignVCenter
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 10
            clip: true
        }

        Text {
            visible: root.shortcutText.length > 0
            text: root.shortcutText
            color: root.enabled ? theme.textMuted : theme.alpha(theme.textMuted, 0.42)
            font.family: theme.fontMono
            font.pixelSize: theme.typeCaption
            font.letterSpacing: 0
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 10
            Layout.maximumWidth: 72
            clip: true
        }
    }

    background: Rectangle {
        radius: theme.radiusControl
        color: !root.enabled
               ? "transparent"
               : root.down
                 ? theme.actionSecondaryPressed
                 : root.highlighted
                   ? theme.actionSecondaryHover
                   : "transparent"
        border.width: root.visualFocus ? theme.focusBorderWidth : 0
        border.color: theme.focusRing
    }

    Accessible.description: root.tooltipText

    AppToolTip {
        id: appToolTip
        targetItem: root
        followPointer: true
        visible: root.hovered && root.tooltipText.length > 0
        delay: 200
        timeout: 2500
        text: root.tooltipText
    }

    HoverHandler {
        onPointChanged: {
            if (appToolTip.parent)
                appToolTip.pointerPosition = root.mapToItem(appToolTip.parent,
                                                            point.position.x, point.position.y)
        }
    }
}
