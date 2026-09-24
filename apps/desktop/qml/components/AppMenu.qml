import QtQuick
import QtQuick.Controls.Basic

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

    background: PopupSurface {
        implicitWidth: root.minimumMenuWidth
        radius: theme.radiusPanel
        fillColor: theme.alpha(theme.glassBase, 0.98)
        borderColor: theme.alpha(theme.glassEdge, 0.34)
    }

    Connections {
        target: root.parent
        ignoreUnknownSignals: true

        function onVisibleChanged() {
            if (root.parent && !root.parent.visible)
                root.close()
        }
    }
}
