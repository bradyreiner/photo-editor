import QtQuick
import QtQuick.Controls.Basic
import "TransientPopupRegistry.js" as TransientPopupRegistry

Menu {
    id: root

    property color accent: theme.focusRing
    property int minimumMenuWidth: theme.menuMinWidth
    property bool transientMenu: false
    property Item transientOpener: null
    property Item transientPressSource: null
    property bool restoreFocusOnClose: false
    property int restoreFocusReason: Qt.TabFocusReason
    property bool suppressNextOpenerActivation: false
    property Item suppressedOpener: null
    property Item suppressedPressSource: null
    readonly property string appMenuContract: "app-menu:v1|shared-surface:true|keyboard:true|escape:true|premium-border:true|consistent-padding:true|readable-items:true"

    Theme { id: theme }

    function rememberOpener(opener, pressSource) {
        if (opener)
            root.transientOpener = opener
        if (pressSource)
            root.transientPressSource = pressSource
    }

    function openTransient(opener, pressSource) {
        root.rememberOpener(opener, pressSource)
        root.transientMenu = true
        root.restoreFocusOnClose = false
        TransientPopupRegistry.claim(root)
        root.open()
    }

    function clearForwardedActivationAfterRelease(pressSource) {
        if (!pressSource)
            return
        Qt.callLater(function() {
            if (root.suppressNextOpenerActivation &&
                    root.suppressedPressSource === pressSource &&
                    pressSource.down !== true && pressSource.pressed !== true) {
                root.suppressNextOpenerActivation = false
                root.suppressedOpener = null
                root.suppressedPressSource = null
            }
        })
    }

    function toggleFrom(opener, pressSource) {
        root.rememberOpener(opener, pressSource)
        root.transientMenu = true
        if (root.suppressNextOpenerActivation) {
            const isForwardedActivation = !opener || root.suppressedOpener === opener
            const suppressedSource = root.suppressedPressSource
            root.suppressNextOpenerActivation = false
            root.suppressedOpener = null
            root.suppressedPressSource = null
            root.clearForwardedActivationAfterRelease(suppressedSource)
            if (isForwardedActivation) {
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
        root.openTransient(opener, pressSource)
        return true
    }

    function scheduleOpenerFocusRestore(reason) {
        const opener = root.transientOpener
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
        root.close()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.transientMenu && root.visible
        onActivated: root.closeTransient(true)
    }

    implicitWidth: Math.max(minimumMenuWidth, contentItem.implicitWidth + theme.space8)
    margins: theme.space8
    padding: theme.space4
    closePolicy: root.transientMenu ? Popup.CloseOnPressOutside
                                    : Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    onAboutToHide: {
        if (!root.transientMenu)
            return
        // Let the opener's press handler run first. This preserves a toggle
        // when Popup forwards the outside press to a different trigger item.
        Qt.callLater(function() {
            const opener = root.transientOpener
            const pressSource = root.transientPressSource || opener
            if (opener && pressSource &&
                    (pressSource.down === true || pressSource.pressed === true)) {
                root.suppressedOpener = opener
                root.suppressedPressSource = pressSource
                root.suppressNextOpenerActivation = true
            }
        })
    }
    onAboutToShow: {
        if (root.transientMenu) {
            TransientPopupRegistry.observeFocus(root)
            TransientPopupRegistry.claim(root)
        }
    }
    onClosed: {
        const shouldRestoreFocus = root.restoreFocusOnClose
        const focusReason = root.restoreFocusReason
        root.restoreFocusOnClose = false
        if (root.transientMenu) {
            TransientPopupRegistry.release(root)
            TransientPopupRegistry.releaseFocus(root)
        }
        if (shouldRestoreFocus)
            root.scheduleOpenerFocusRestore(focusReason)
    }

    background: PopupSurface { implicitWidth: root.minimumMenuWidth }
    font.family: theme.fontUi
    font.pixelSize: theme.typeBody
    enter: PopupEnterTransition {}
    exit: PopupExitTransition {}

    Connections {
        target: root.parent
        ignoreUnknownSignals: true

        function onVisibleChanged() {
            if (root.parent && !root.parent.visible)
                root.close()
        }
    }

    Connections {
        target: root.suppressedPressSource
        ignoreUnknownSignals: true
        function onDownChanged() {
            if (target && target.down !== true)
                root.clearForwardedActivationAfterRelease(target)
        }
        function onPressedChanged() {
            if (target && target.pressed !== true)
                root.clearForwardedActivationAfterRelease(target)
        }
    }

    Component.onDestruction: {
        if (root.transientMenu)
            TransientPopupRegistry.release(root)
        TransientPopupRegistry.releaseFocus(root)
        root.suppressNextOpenerActivation = false
        root.suppressedOpener = null
        root.suppressedPressSource = null
    }
}
