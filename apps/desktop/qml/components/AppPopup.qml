import QtQuick
import QtQuick.Controls.Basic
import "TransientPopupRegistry.js" as TransientPopupRegistry

Popup {
    id: root
    Theme { id: popupTheme }
    property bool transientMenu: false
    property Item viewportBoundsItem: null
    property Item transientOpener: null
    property Item transientFocusTarget: null
    property bool restoreFocusOnClose: false
    property int restoreFocusReason: Qt.TabFocusReason
    property bool explicitCloseInProgress: false
    property bool suppressNextOpenerActivation: false
    property Item suppressedOpener: null
    readonly property real popupAvailableWidth: Math.max(1, root.viewportBoundsItem
        ? root.viewportBoundsItem.width - root.margins * 2 : root.Overlay.overlay
        ? root.Overlay.overlay.width - root.margins * 2 : parent ? parent.width - root.margins * 2 : 1920)
    readonly property real popupAvailableHeight: Math.max(1, root.viewportBoundsItem
        ? root.viewportBoundsItem.height - root.margins * 2 : root.Overlay.overlay
        ? root.Overlay.overlay.height - root.margins * 2 : parent ? parent.height - root.margins * 2 : 1080)
    readonly property string appPopupContract: "app-popup:v1|surface:shared-neutral|motion:shared|dim:shared|bounds:viewport|policies:owner-preserved"
    readonly property int standardPopupPadding: popupTheme.popupPadding
    margins: popupTheme.space12
    padding: popupTheme.popupPadding
    dim: modal
    font.family: popupTheme.fontUi
    font.pixelSize: popupTheme.typeBody
    height: Math.min(popupAvailableHeight, implicitHeight)
    // Transient Escape goes through closeTransient so focus return has one owner.
    closePolicy: (root.transientMenu ? 0 : Popup.CloseOnEscape) | Popup.CloseOnPressOutside
    background: PopupSurface {}
    Overlay.modal: Rectangle { color: popupTheme.popupDim }
    enter: PopupEnterTransition {}
    exit: PopupExitTransition {}

    function pointerOpenerIsPressed(opener) {
        return !!opener && (opener.down === true || opener.pressed === true)
    }

    function pointerOpenerIsOverOrPressed(opener) {
        return !!opener && (opener.hovered === true || opener.containsMouse === true ||
                            pointerOpenerIsPressed(opener))
    }

    function clearOpenerSuppressionAfterRelease(opener) {
        if (!opener)
            return
        Qt.callLater(function() {
            if (root.suppressNextOpenerActivation && root.suppressedOpener === opener &&
                    !root.pointerOpenerIsPressed(opener)) {
                root.suppressNextOpenerActivation = false
                root.suppressedOpener = null
            }
        })
    }

    function openTransient(opener, focusTarget) {
        if (opener)
            root.transientOpener = opener
        if (focusTarget)
            root.transientFocusTarget = focusTarget
        else if (opener)
            root.transientFocusTarget = opener
        root.transientMenu = true
        root.restoreFocusOnClose = false
        root.explicitCloseInProgress = false
        TransientPopupRegistry.claim(root)
        root.open()
    }

    function toggleTransient(opener, focusTarget) {
        if (opener)
            root.transientOpener = opener
        if (focusTarget)
            root.transientFocusTarget = focusTarget
        else if (opener)
            root.transientFocusTarget = opener
        root.transientMenu = true
        if (root.suppressNextOpenerActivation) {
            const isForwardedOpenerActivation =
                !opener || root.suppressedOpener === opener
            root.suppressNextOpenerActivation = false
            root.suppressedOpener = null
            if (isForwardedOpenerActivation) {
                root.restoreFocusOnClose = true
                root.restoreFocusReason = Qt.MouseFocusReason
                root.scheduleOpenerFocusRestore(Qt.MouseFocusReason)
                return false
            }
        }
        if (root.visible) {
            root.closeTransient(true)
            return false
        }
        root.openTransient(opener, focusTarget)
        return true
    }

    function scheduleOpenerFocusRestore(reason) {
        const opener = root.transientFocusTarget || root.transientOpener
        if (!opener)
            return
        Qt.callLater(function() {
            if (!root.visible && !TransientPopupRegistry.activePopup &&
                    !TransientPopupRegistry.focusedPopup && opener.visible)
                opener.forceActiveFocus(reason)
        })
    }

    function closeTransient(restoreFocus) {
        root.restoreFocusReason = Qt.TabFocusReason
        root.restoreFocusOnClose = restoreFocus === true
        root.explicitCloseInProgress = root.visible
        root.close()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.transientMenu && root.visible
        onActivated: root.closeTransient(true)
    }

    Connections {
        target: root.suppressedOpener
        ignoreUnknownSignals: true
        function onDownChanged() {
            root.clearOpenerSuppressionAfterRelease(target)
        }
        function onPressedChanged() {
            root.clearOpenerSuppressionAfterRelease(target)
        }
    }

    onAboutToShow: {
        TransientPopupRegistry.observeFocus(root)
        if (root.transientMenu)
            TransientPopupRegistry.claim(root)
    }

    onAboutToHide: {
        // CloseOnPressOutside dismisses before the same press reaches the
        // opener. Observe the settled press state so toggleTransient can
        // consume that one forwarded activation instead of reopening.
        const opener = root.transientOpener
        if (!root.transientMenu || root.explicitCloseInProgress ||
                !root.pointerOpenerIsOverOrPressed(opener))
            return
        Qt.callLater(function() {
            if (root.pointerOpenerIsPressed(opener)) {
                root.suppressedOpener = opener
                root.suppressNextOpenerActivation = true
            }
        })
    }

    onClosed: {
        const shouldRestoreFocus = root.restoreFocusOnClose
        const focusReason = root.restoreFocusReason
        root.restoreFocusOnClose = false
        root.explicitCloseInProgress = false
        if (root.transientMenu)
            TransientPopupRegistry.release(root)
        TransientPopupRegistry.releaseFocus(root)
        if (shouldRestoreFocus)
            root.scheduleOpenerFocusRestore(focusReason)
    }

    Component.onDestruction: {
        if (root.transientMenu)
            TransientPopupRegistry.release(root)
        TransientPopupRegistry.releaseFocus(root)
    }
}
