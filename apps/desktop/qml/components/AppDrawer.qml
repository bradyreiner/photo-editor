import QtQuick
import QtQuick.Controls.Basic

Drawer {
    Theme { id: drawerTheme }
    font.family: drawerTheme.fontUi
    font.pixelSize: drawerTheme.typeBody
    dim: modal
    background: PopupSurface { radius: 0; shadowColor: "transparent" }
    Overlay.modal: Rectangle { color: drawerTheme.popupDim }
}
