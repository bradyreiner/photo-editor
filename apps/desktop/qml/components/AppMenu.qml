import QtQuick
import QtQuick.Controls

Menu {
    id: root

    property color accent: theme.focusRing
    property int minimumMenuWidth: theme.menuMinWidth
    readonly property string appMenuContract: "app-menu:v1|shared-surface:true|keyboard:true|escape:true|premium-border:true|consistent-padding:true|readable-items:true"

    Theme { id: theme }

    implicitWidth: Math.max(minimumMenuWidth, contentItem.implicitWidth + theme.space8)
    margins: theme.space8
    padding: theme.space4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        implicitWidth: root.minimumMenuWidth
        radius: theme.radiusPanel
        color: theme.alpha(theme.glassBase, 0.98)
        border.width: 1
        border.color: theme.alpha(theme.glassEdge, 0.34)

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 1
            height: 1
            color: theme.alpha(theme.polar, 0.08)
        }
    }
}
