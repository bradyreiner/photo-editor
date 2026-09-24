import QtQml

QtObject {
    id: root

    required property var previewController
    required property var nameEditor

    function select(flatIndex) {
        root.previewController.activeMaskIndex = flatIndex
    }

    function rename(flatIndex) {
        root.select(flatIndex)
        root.nameEditor.forceActiveFocus()
        root.nameEditor.selectAll()
    }

    function toggleEnabled(flatIndex, enabled) {
        root.select(flatIndex)
        root.previewController.activeMaskEnabled = !enabled
    }

    function toggleSolo(flatIndex, solo) {
        root.select(flatIndex)
        root.previewController.activeMaskSolo = !solo
    }

    function toggleLocked(flatIndex, locked) {
        root.select(flatIndex)
        root.previewController.activeMaskLocked = !locked
    }

    function move(flatIndex, offset) {
        root.select(flatIndex)
        root.previewController.moveActiveMask(offset)
    }

    function duplicate(flatIndex) {
        root.select(flatIndex)
        root.previewController.duplicateActiveMask()
    }

    function remove(flatIndex) {
        root.select(flatIndex)
        root.previewController.removeActiveMask()
    }

    function applyRetouch(flatIndex) {
        root.select(flatIndex)
        root.previewController.applyActiveRetouch()
    }

    function cancelRetouch(flatIndex) {
        root.select(flatIndex)
        root.previewController.cancelActiveRetouch()
    }
}
