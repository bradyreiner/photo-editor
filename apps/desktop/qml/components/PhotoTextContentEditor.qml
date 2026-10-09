pragma ComponentBehavior: Bound
import QtQuick
import "." as Components

Components.ToneTextArea {
    id: root

    required property var controller
    required property int layerIndex
    required property var layerData
    property bool locked: false
    property bool synchronizing: false
    property string editError: ""
    property bool transactionOpen: false
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
        if (transactionOpen) {
            transactionOpen = false
            controller.endCoalescedEdit()
        }
    }

    function synchronize() {
        const saved = String(layerData.text === undefined ? "" : layerData.text)
        if (text === saved) return
        synchronizing = true
        const position = cursorPosition
        text = saved
        cursorPosition = Math.min(position, text.length)
        synchronizing = false
    }

    function forceInputFocus() { forceActiveFocus() }
    function clearInputFocus() { focus = false; endEditing() }

    onTargetIdChanged: { clearInputFocus(); editError = ""; synchronize() }
    onLayerDataChanged: synchronize()
    onVisibleChanged: { if (!visible) clearInputFocus() }
    onEnabledChanged: { if (!enabled) clearInputFocus() }
    onActiveFocusChanged: { if (!activeFocus) endEditing() }
    onTextChanged: {
        if (synchronizing || !activeFocus || locked || String(layerData.kind || "") !== "text") return
        if (text === String(layerData.text === undefined ? "" : layerData.text)) return
        if (!transactionOpen) {
            transactionOpen = true
            controller.beginCoalescedEdit()
        }
        if (controller.setPhotoLayerText(layerIndex, text)) editError = ""
        else {
            editError = qsTr("Could not apply the text change. The saved text is shown.")
            synchronize()
        }
    }
    Keys.onEscapePressed: function(event) { clearInputFocus(); event.accepted = true }
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) &&
                (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) {
            clearInputFocus()
            event.accepted = true
        }
    }
    Component.onCompleted: synchronize()
    Component.onDestruction: endEditing()
}
