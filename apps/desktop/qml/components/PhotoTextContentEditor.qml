pragma ComponentBehavior: Bound
import QtQuick
import "." as Components

Components.ToneTextArea {
    id: root

    required property var controller
    required property int layerIndex
    required property var layerData
    // Optional alternate input surface for the canonical canvas session.
    property var session: null
    property bool locked: false
    property bool synchronizing: false
    property string editError: ""
    property bool transactionOpen: false
    property bool pendingEdit: false
    property string pendingText: ""
    property string transactionLayerId: ""
    readonly property string targetId: String(layerData.id || "")
    readonly property bool userEditing: activeFocus

    objectName: "photoLayerTextContentField"
    accessibleLabel: qsTr("Text content")
    placeholderText: qsTr("Write your text…")
    textFormat: TextEdit.PlainText
    persistentSelection: true
    enabled: !locked
    clip: true

    function endEditing() {
        if (session) return session.finish(true)
        if (!flushPendingText()) return false
        if (transactionOpen) {
            transactionOpen = false
            controller.endCoalescedEdit()
        }
        transactionLayerId = ""
        return true
    }

    function commitPendingText() {
        if (session) return !session.editing || session.flushPendingText()
        editTimer.stop()
        if (!pendingEdit) return true
        const nextText = pendingText
        pendingEdit = false
        const targetId = transactionLayerId.length > 0 ? transactionLayerId : root.targetId
        const saved = String(layerData.text === undefined ? "" : layerData.text)
        if (targetId === root.targetId && nextText === saved) {
            pendingText = ""
            return true
        }
        const applied = typeof controller.setPhotoLayerTextById === "function"
            ? controller.setPhotoLayerTextById(targetId, nextText)
            : targetId === root.targetId && controller.setPhotoLayerText(layerIndex, nextText)
        if (applied) {
            editError = ""
            pendingText = ""
            return true
        }
        editError = qsTr("Could not save this text. Your draft is retained; retry Done or cancel.")
        pendingText = nextText
        pendingEdit = true
        return false
    }

    function flushPendingText() {
        return commitPendingText()
    }

    function synchronize() {
        if (session) {
            const value = session.editing ? session.draftText : String(layerData.text || "")
            if (text === value) return
            synchronizing = true
            const start = selectionStart
            const end = selectionEnd
            text = value
            select(Math.min(start, text.length), Math.min(end, text.length))
            synchronizing = false
            return
        }
        // A new target cannot acknowledge the previous target's pending edit,
        // even when both happen to contain the same copy. The target-change
        // handler flushes that edit by ID before loading the new text.
        if (pendingEdit && transactionLayerId.length > 0 &&
                transactionLayerId !== String(layerData.id || "")) return
        const saved = String(layerData.text === undefined ? "" : layerData.text)
        if (pendingEdit && saved !== pendingText) return
        if (pendingEdit && saved === pendingText) {
            pendingEdit = false
            pendingText = ""
            editTimer.stop()
        }
        if (text === saved) return
        synchronizing = true
        const position = cursorPosition
        text = saved
        cursorPosition = Math.min(position, text.length)
        synchronizing = false
    }

    function forceInputFocus() { forceActiveFocus() }
    function clearInputFocus() {
        if (session) {
            if (session.editing && !session.flushPendingText()) return false
            focus = false
            return true
        }
        if (!endEditing()) return false
        focus = false
        return true
    }
    function cancelEditing() {
        if (session) return session.finish(false)
        editTimer.stop()
        pendingEdit = false
        pendingText = ""
        if (transactionOpen) controller.cancelCoalescedEdit()
        transactionOpen = false
        transactionLayerId = ""
        editError = ""
        synchronize()
        focus = false
    }

    Connections {
        target: root.session
        function onDraftTextChanged() { root.synchronize() }
        function onEditingChanged() { root.synchronize() }
    }

    onTargetIdChanged: {
        const editClosed = clearInputFocus()
        if (editClosed) editError = ""
        synchronize()
    }
    onLayerDataChanged: synchronize()
    onVisibleChanged: { if (!visible) clearInputFocus() }
    onEnabledChanged: { if (!enabled) clearInputFocus() }
    onActiveFocusChanged: {
        if (session) {
            if (activeFocus) {
                if (!session.editing && !session.beginEditingFromInspector()) return
                session.selectionEditor = root
            }
        } else if (!activeFocus) endEditing()
    }
    onTextChanged: {
        if (synchronizing || !activeFocus || locked || String(layerData.kind || "") !== "text") return
        if (session) {
            session.updateDraft(text)
            return
        }
        if (text === String(layerData.text === undefined ? "" : layerData.text)) {
            pendingEdit = false
            pendingText = ""
            editTimer.stop()
            if (transactionOpen) {
                transactionOpen = false
                controller.endCoalescedEdit()
            }
            transactionLayerId = ""
            return
        }
        if (!transactionOpen) {
            transactionOpen = true
            transactionLayerId = root.targetId
            controller.beginCoalescedEdit()
        }
        pendingText = text
        pendingEdit = true
        editError = ""
        editTimer.restart()
    }
    Timer {
        id: editTimer
        interval: 45
        repeat: false
        onTriggered: root.commitPendingText()
    }
    Keys.onShortcutOverride: function(event) { event.accepted = true }
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function(event) {
        if (session) { session.handleEditorKey(event); return }
        if (event.key === Qt.Key_Escape) {
            if (inputMethodComposing) Qt.inputMethod.reset()
            else cancelEditing()
            event.accepted = true
            return
        }
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) &&
                (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) {
            clearInputFocus()
            event.accepted = true
        }
    }
    Component.onCompleted: synchronize()
    Component.onDestruction: endEditing()
}
