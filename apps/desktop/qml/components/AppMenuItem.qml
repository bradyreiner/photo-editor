import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

MenuItem {
    id: root

    property string iconName: ""
    property string shortcutText: ""
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
                     : (root.highlighted ? root.accent : theme.textMain)
            Layout.preferredWidth: theme.typeSection
            Layout.preferredHeight: theme.typeSection
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            Layout.fillWidth: true
            text: root.text
            color: !root.enabled
                   ? theme.alpha(theme.textMuted, 0.52)
                   : root.danger
                     ? theme.error
                     : theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeBody
            font.weight: root.highlighted ? Font.DemiBold : Font.Medium
            font.letterSpacing: 0
            verticalAlignment: Text.AlignVCenter
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 9
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
            minimumPixelSize: 8
            Layout.maximumWidth: 72
            clip: true
        }
    }

    background: Rectangle {
        radius: theme.radiusControl
        color: !root.enabled
               ? "transparent"
               : root.down
                 ? theme.alpha(root.accent, 0.18)
                 : root.highlighted
                   ? theme.alpha(root.accent, 0.12)
                   : "transparent"
        border.width: root.visualFocus ? theme.hairlineWidth : 0
        border.color: theme.alpha(theme.focusRing, 0.82)
    }
}
