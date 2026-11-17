import QtQuick
import QtQuick.Controls.Basic

Drawer {
    Theme { id: drawerTheme }
    font.family: drawerTheme.fontUi
    font.pixelSize: drawerTheme.typeBody
    dim: modal
    background: PopupSurface { docked: true; shadowColor: "transparent" }
    Overlay.modal: Rectangle { color: drawerTheme.popupDim }
}
