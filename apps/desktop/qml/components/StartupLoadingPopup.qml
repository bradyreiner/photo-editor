pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root
    objectName: "startupLoadingPopup"

    property bool catalogLoading: false
    property bool initialized: false
    property bool observedLoading: false
    property bool finished: false

    function synchronize() {
        if (!initialized || finished)
            return
        if (catalogLoading) {
            observedLoading = true
            open()
        } else if (observedLoading) {
            finished = true
            close()
        }
    }

    Component.onCompleted: {
        initialized = true
        synchronize()
    }
    onCatalogLoadingChanged: synchronize()

    Theme { id: theme }
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: standardPopupPadding
    width: Math.min(popupAvailableWidth, Math.max(1, Math.min(560, parent ? Math.max(240, Math.min(parent.width - 32, (parent.height - 100) * 1.5)) : 560)))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    background: Item {}
    // Release the modal as soon as the catalog is ready, even if the render
    // loop is busy preparing the first library frame.
    exit: Transition {}

    contentItem: LooksyLoadingScene {
        objectName: "startupLoadingScene"
        scene: "startup"
        active: root.visible && root.catalogLoading
        title: "Opening your library"
        detail: "Loading photos and saved edits."
    }
}
