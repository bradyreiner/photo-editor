import QtQuick
import QtQuick.Controls.Basic

Dialog {
    id: root
    Theme { id: dialogTheme }
    readonly property real popupAvailableWidth: Math.max(1, root.Overlay.overlay
        ? root.Overlay.overlay.width - root.margins * 2 : parent ? parent.width - root.margins * 2 : 1920)
    readonly property real popupAvailableHeight: Math.max(1, root.Overlay.overlay
        ? root.Overlay.overlay.height - root.margins * 2 : parent ? parent.height - root.margins * 2 : 1080)
    readonly property string appDialogContract: "app-dialog:v1|surface:shared-neutral|header:shared|actions:shared|motion:shared|bounds:viewport|policies:owner-preserved"
    readonly property int standardPopupPadding: dialogTheme.panelPaddingStandard
    margins: dialogTheme.space12
    padding: dialogTheme.panelPaddingStandard
    spacing: 0
    modal: true
    focus: true
    dim: modal
    font.family: dialogTheme.fontUi
    font.pixelSize: dialogTheme.typeBody
    // Wrapped headers follow the available width; they must not also drive it.
    implicitWidth: Math.min(popupAvailableWidth, Math.max(320,
        implicitContentWidth + leftPadding + rightPadding, implicitFooterWidth))
    height: Math.min(popupAvailableHeight, implicitHeight)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: PopupSurface {}
    Overlay.modal: Rectangle { color: dialogTheme.popupDim }
    enter: PopupEnterTransition {}
    exit: PopupExitTransition {}
    header: Label {
        visible: root.title.length > 0
        text: root.title
        color: dialogTheme.textStrong
        font.family: dialogTheme.fontUi
        font.pixelSize: dialogTheme.typeDialogTitle
        font.weight: Font.DemiBold
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        leftPadding: dialogTheme.panelPaddingStandard
        rightPadding: dialogTheme.panelPaddingStandard
        topPadding: dialogTheme.panelPaddingStandard
        bottomPadding: 0
    }
    footer: DialogButtonBox {
        visible: root.standardButtons !== Dialog.NoButton
        standardButtons: root.standardButtons
        alignment: Qt.AlignRight
        spacing: dialogTheme.space8
        padding: dialogTheme.panelPaddingStandard
        topPadding: 0
        background: Item {}
        delegate: ActionButton {
            width: Math.max(dialogTheme.scaled(72), implicitWidth)
            tone: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                  || DialogButtonBox.buttonRole === DialogButtonBox.YesRole ? "primary" : "secondary"
        }
    }
}
