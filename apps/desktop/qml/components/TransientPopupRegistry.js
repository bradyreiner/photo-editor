.pragma library

// Keep lightweight menu and selector popups mutually exclusive within one
// QML engine. App dialogs opt out so opening a workflow dialog is independent.
var activePopup = null
var focusedPopup = null

function claim(popup) {
    if (!popup)
        return

    var previous = activePopup
    if (previous === popup)
        return

    activePopup = popup
    if (previous && previous.visible && typeof previous.close === "function")
        previous.close()
}

function release(popup) {
    if (activePopup === popup)
        activePopup = null
}

function observeFocus(popup) {
    focusedPopup = popup
}

function releaseFocus(popup) {
    if (focusedPopup === popup)
        focusedPopup = null
}
