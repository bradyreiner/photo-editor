import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root
    required property var theme
    required property var documentController
    property color noteAccent: "#47b5ff"
    property real frameRate: 30
    property real startFrame: 0
    property real endFrame: 0
    property var clipIds: []
    property string targetLabel: "Timeline"
    property string errorText: ""
    property string anchorError: ""
    property alias noteText: body.text
    signal noteCreated(string markerId)

    width: Math.min(popupAvailableWidth, theme.scaled(340))
    modal: false
    focus: true

    function beginDraft(firstFrame, lastFrame, ids, label) {
        startFrame = firstFrame
        endFrame = lastFrame
        // Freeze the context: selection and playhead changes cannot retarget a draft.
        clipIds = (ids || []).slice()
        targetLabel = label || "Timeline"
        body.text = ""
        errorText = ""
        anchorError = ""
        open()
    }

    function changeAnchorKind(range) {
        if (!range) {
            endFrame = startFrame
            anchorError = ""
            return true
        }
        const fps = Math.max(1, frameRate)
        const maximum = Math.round(Number(documentController.totalDurationSeconds || 0) * fps)
        const last = maximum > 0 ? Math.min(maximum, startFrame + Math.round(fps)) : startFrame + Math.round(fps)
        if (last <= startFrame) {
            anchorError = "Choose a start time before the end of the timeline."
            return false
        }
        endFrame = last
        anchorError = ""
        return true
    }

    function commitAnchorText(value, end) {
        const text = String(value).trim()
        const seconds = text.length > 0 ? Number(text) : NaN
        const fps = Math.max(1, frameRate)
        const next = Math.round(seconds * fps)
        const maximum = Math.round(Number(documentController.totalDurationSeconds || 0) * fps)
        if (!Number.isFinite(seconds) || seconds < 0 || (maximum > 0 && next > maximum)
                || (end && next <= startFrame)) {
            anchorError = end ? "End time must follow the start and stay inside the timeline."
                              : "Enter a time inside the timeline."
            return false
        }
        const range = endFrame > startFrame
        if (end) endFrame = next
        else {
            if (range && maximum > 0 && next >= maximum) {
                anchorError = "Choose a start time before the end of the timeline."
                return false
            }
            startFrame = next
            endFrame = range ? Math.max(endFrame, next + 1) : next
        }
        anchorError = ""
        return true
    }

    function saveNote() {
        if (body.text.trim().length === 0 || anchorError.length > 0)
            return false
        const id = String(documentController.addAnchoredReviewNote(
            "", body.text.trim(), startFrame, endFrame, clipIds, "", "open") || "")
        if (id.length === 0) {
            errorText = String(documentController.status || "Could not save this note.")
            return false
        }
        noteCreated(id)
        close()
        return true
    }

    onOpened: body.forceActiveFocus()
    contentItem: ColumnLayout {
        spacing: root.theme.space10
        RowLayout {
            AppIcon { name: "edit"; color: root.noteAccent }
            Label {
                text: "Add a note"
                color: root.noteAccent
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeBody
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Label {
                text: (root.startFrame / Math.max(1, root.frameRate)).toFixed(2)
                      + (root.endFrame > root.startFrame
                         ? "–" + (root.endFrame / Math.max(1, root.frameRate)).toFixed(2) : "") + "s"
                color: root.theme.textMuted
                font.family: root.theme.fontNumeric
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.targetLabel
            color: root.theme.textMuted
            font.family: root.theme.fontUi
            font.pixelSize: root.theme.typeCaption
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.space4
            ToneComboBox {
                objectName: "timelineNoteAnchorKind"
                Layout.preferredWidth: root.theme.scaled(100)
                accessibleLabel: "Note anchor"
                model: ["Frame", "Range"]
                currentIndex: root.endFrame > root.startFrame ? 1 : 0
                onActivated: function(index) { root.changeAnchorKind(index === 1) }
            }
            ToneTextField {
                objectName: "timelineNoteStartTime"
                Layout.fillWidth: true
                Layout.minimumWidth: root.theme.scaled(66)
                accessibleLabel: "Note start time in seconds"
                numericEntry: true
                text: (root.startFrame / Math.max(1, root.frameRate)).toFixed(3)
                onEditingFinished: root.commitAnchorText(text, false)
            }
            Label {
                text: root.endFrame > root.startFrame ? "–" : "s"
                color: root.theme.textMuted
            }
            ToneTextField {
                objectName: "timelineNoteEndTime"
                visible: root.endFrame > root.startFrame
                Layout.fillWidth: true
                Layout.minimumWidth: root.theme.scaled(66)
                accessibleLabel: "Note end time in seconds"
                numericEntry: true
                text: (root.endFrame / Math.max(1, root.frameRate)).toFixed(3)
                onEditingFinished: root.commitAnchorText(text, true)
            }
        }
        ToneTextArea {
            id: body
            objectName: "timelineQuickNoteText"
            Layout.fillWidth: true
            Layout.preferredHeight: root.theme.scaled(96)
            placeholderText: "What should change here?"
            wrapMode: TextEdit.Wrap
            Accessible.name: "Timeline note"
            Keys.onPressed: function(event) {
                if ((event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
                        && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                    root.saveNote()
                    event.accepted = true
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.errorText.length > 0 || root.anchorError.length > 0
            text: root.anchorError.length > 0 ? root.anchorError : root.errorText
            color: root.theme.warning
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { text: "Cancel"; compact: true; onClicked: root.close() }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "timelineQuickNoteSave"
                text: "Add note"
                iconName: "plus"
                accent: root.noteAccent
                tone: "primary"
                compact: true
                enabled: body.text.trim().length > 0
                onClicked: root.saveNote()
            }
        }
    }
}
