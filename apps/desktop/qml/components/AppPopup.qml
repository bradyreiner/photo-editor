import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: root
    Theme { id: popupTheme }
    readonly property real popupAvailableWidth: Math.max(1, root.Overlay.overlay
        ? root.Overlay.overlay.width - root.margins * 2 : parent ? parent.width - root.margins * 2 : 1920)
    readonly property real popupAvailableHeight: Math.max(1, root.Overlay.overlay
        ? root.Overlay.overlay.height - root.margins * 2 : parent ? parent.height - root.margins * 2 : 1080)
    readonly property string appPopupContract: "app-popup:v1|surface:shared-neutral|motion:shared|dim:shared|bounds:viewport|policies:owner-preserved"
    readonly property int standardPopupPadding: popupTheme.panelPaddingStandard
    margins: popupTheme.space12
    padding: popupTheme.panelPaddingStandard
    dim: modal
    font.family: popupTheme.fontUi
    font.pixelSize: popupTheme.typeBody
    height: Math.min(popupAvailableHeight, implicitHeight)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: PopupSurface {}
    Overlay.modal: Rectangle { color: popupTheme.popupDim }
    enter: PopupEnterTransition {}
    exit: PopupExitTransition {}
}
