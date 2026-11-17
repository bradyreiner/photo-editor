pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

GlassSurface {
    id: root

    required property var theme
    required property var documentController
    required property var currentClip
    required property real playheadSeconds
    required property real frameRate
    property real inPointSeconds: -1
    property real outPointSeconds: -1
    property string filter: "open"
    property string anchorMode: "frame"
    property string draftLabel: ""
    property string draftNote: ""
    property bool includeCurrentClip: false
    property string editingMarkerId: ""
    property string editLabel: ""
    property string editNote: ""
    property string editStartText: ""
    property string editEndText: ""
    property string editApprovalState: "open"
    property string editAssignee: ""
    property var editClipIds: []
    property string statusMessage: ""
    property alias draftRangeStartText: rangeStartField.text
    property alias draftRangeEndText: rangeEndField.text

    readonly property string timelineReviewPanelContract:
        "timeline-review-panel:v1|storage:existing-marker-review-notes|anchors:frame-or-half-open-range|clip-context:optional-stable-ids|filter:all-open-resolved|actions:jump-edit-save-resolve-reopen|status:visible|no-execution-controls:true"
    readonly property var notes: documentController ? documentController.reviewNotes : []
    readonly property var visibleNotes: {
        const source = notes || []
        const result = []
        for (let index = 0; index < source.length; ++index) {
            const note = source[index] || ({})
            const resolved = note.resolved === true || note.completed === true
            if (filter === "open" && resolved)
                continue
            if (filter === "resolved" && !resolved)
                continue
            result.push(note)
        }
        return result
    }
    readonly property bool activeTimeline: documentController
                                            && documentController.hasActiveTimeline === true
    readonly property real safeFrameRate: Math.max(1, Number(frameRate || 30))
    readonly property real totalDurationSeconds: Math.max(
        0, Number(documentController ? documentController.totalDurationSeconds : 0))
    readonly property string currentClipId: String(currentClip && currentClip.clipId || "")
    readonly property string currentClipLabel: String(
        currentClip && (currentClip.displayLabel || currentClip.titleText) || "")
    readonly property bool validInOutRange: Number.isFinite(Number(inPointSeconds))
                                            && Number.isFinite(Number(outPointSeconds))
                                            && Number(inPointSeconds) >= 0
                                            && Number(outPointSeconds) > Number(inPointSeconds)
    readonly property int openNoteCount: {
        const source = notes || []
        let count = 0
        for (let index = 0; index < source.length; ++index) {
            const note = source[index] || ({})
            if (note.resolved !== true && note.completed !== true)
                ++count
        }
        return count
    }
    readonly property int resolvedNoteCount: Math.max(0, (notes || []).length - openNoteCount)

    signal jumpRequested(real seconds)
    signal noteCreated(string markerId)
    signal noteUpdated(string markerId)

    implicitWidth: 340
    implicitHeight: 500
    radius: 0
    edgeOpacity: 0.10
    highlightOpacity: 0.06
    Accessible.role: Accessible.Pane
    Accessible.name: "Timeline review notes"

    function clampedSeconds(value) {
        const seconds = Number(value)
        if (!Number.isFinite(seconds))
            return 0
        return Math.max(0, Math.min(totalDurationSeconds, seconds))
    }

    function frameAt(seconds) {
        return Math.max(0, Math.round(clampedSeconds(seconds) * safeFrameRate))
    }

    function secondsAtFrame(frame) {
        return Math.max(0, Number(frame || 0)) / safeFrameRate
    }

    function timecode(seconds) {
        const totalFrames = frameAt(seconds)
        const nominalFps = Math.max(1, Math.round(safeFrameRate))
        const hours = Math.floor(totalFrames / (3600 * nominalFps))
        const minutes = Math.floor((totalFrames % (3600 * nominalFps)) / (60 * nominalFps))
        const wholeSeconds = Math.floor((totalFrames % (60 * nominalFps)) / nominalFps)
        const frames = totalFrames % nominalFps
        return String(hours).padStart(2, "0") + ":"
                + String(minutes).padStart(2, "0") + ":"
                + String(wholeSeconds).padStart(2, "0") + ":"
                + String(frames).padStart(2, "0")
    }

    function noteStartSeconds(note) {
        if (note && Number.isFinite(Number(note.frame)))
            return secondsAtFrame(note.frame)
        return Math.max(0, Number(note && note.seconds || 0))
    }

    function noteEndSeconds(note) {
        if (note && String(note.anchorType || "frame") === "range"
                && Number.isFinite(Number(note.endFrame)))
            return secondsAtFrame(note.endFrame)
        return noteStartSeconds(note)
    }

    function noteAnchorText(note) {
        const start = noteStartSeconds(note)
        if (String(note && note.anchorType || "frame") === "range")
            return timecode(start) + " – " + timecode(noteEndSeconds(note))
        return timecode(start)
    }

    function noteStatus(note) {
        if (note && (note.resolved === true || note.completed === true))
            return "Resolved"
        const approval = String(note && note.approvalState || "open")
        if (approval === "changes-requested")
            return "Changes requested"
        if (approval === "approved")
            return "Approved"
        return "Open"
    }

    function noteTone(note) {
        if (note && (note.resolved === true || note.completed === true))
            return "success"
        const approval = String(note && note.approvalState || "open")
        if (approval === "changes-requested")
            return "warning"
        if (approval === "approved")
            return "success"
        return "neutral"
    }

    function setFilter(nextFilter) {
        filter = ["all", "open", "resolved"].indexOf(String(nextFilter)) >= 0
                ? String(nextFilter) : "open"
    }

    function beginRangeDraft() {
        if (validInOutRange) {
            rangeStartField.text = Number(inPointSeconds).toFixed(3)
            rangeEndField.text = Number(outPointSeconds).toFixed(3)
        } else {
            const lastFrameStart = Math.max(0, totalDurationSeconds - 1 / safeFrameRate)
            const start = Math.min(clampedSeconds(playheadSeconds), lastFrameStart)
            const end = Math.min(totalDurationSeconds,
                                 Math.max(start + 1 / safeFrameRate, start + 5))
            rangeStartField.text = start.toFixed(3)
            rangeEndField.text = end.toFixed(3)
        }
        anchorMode = "range"
    }

    function rangeDraftValid(startText, endText) {
        if (String(startText || "").trim().length === 0 ||
                String(endText || "").trim().length === 0)
            return false
        const start = Number(startText)
        const end = Number(endText)
        return Number.isFinite(start) && Number.isFinite(end)
                && start >= 0 && end > start && end <= totalDurationSeconds
    }

    function simplifiedText(value) {
        return String(value || "").trim().replace(/\s+/g, " ")
    }

    function currentDraftAnchor() {
        if (anchorMode === "range") {
            const start = frameAt(Number(rangeStartField.text))
            const end = frameAt(Number(rangeEndField.text))
            return {startFrame: start, endFrame: Math.max(start + 1, end)}
        }
        const frame = frameAt(playheadSeconds)
        return {startFrame: frame, endFrame: frame}
    }

    function createNote() {
        if (!activeTimeline || draftNote.trim().length === 0)
            return false
        if (anchorMode === "range"
                && !rangeDraftValid(rangeStartField.text, rangeEndField.text)) {
            statusMessage = "Set a valid In and Out range before adding this note."
            return false
        }
        const anchor = currentDraftAnchor()
        const clipIds = includeCurrentClip && currentClipId.length > 0
                ? [currentClipId] : []
        const id = String(documentController.addAnchoredReviewNote(
            draftLabel.trim(), draftNote.trim(), anchor.startFrame, anchor.endFrame,
            clipIds, "", "open") || "")
        if (id.length === 0) {
            statusMessage = String(documentController.status || "The review note could not be added.")
            return false
        }
        draftLabel = ""
        draftNote = ""
        includeCurrentClip = false
        anchorMode = "frame"
        filter = "all"
        statusMessage = anchor.endFrame > anchor.startFrame
                ? "Added range note · " + timecode(secondsAtFrame(anchor.startFrame))
                  + "–" + timecode(secondsAtFrame(anchor.endFrame))
                : "Added note at " + timecode(secondsAtFrame(anchor.startFrame))
        noteCreated(id)
        return true
    }

    function beginEdit(note) {
        if (!note)
            return false
        editingMarkerId = String(note.markerId || "")
        editLabel = String(note.label || "")
        editNote = String(note.note || "")
        editStartText = noteStartSeconds(note).toFixed(3)
        editEndText = noteEndSeconds(note).toFixed(3)
        editApprovalState = String(note.approvalState || "open")
        editAssignee = String(note.assignee || "")
        const sourceIds = note.clipIds || []
        const copiedIds = []
        for (let index = 0; index < sourceIds.length; ++index)
            copiedIds.push(String(sourceIds[index] || ""))
        editClipIds = copiedIds
        return editingMarkerId.length > 0
    }

    function cancelEdit() {
        editingMarkerId = ""
        editLabel = ""
        editNote = ""
        editStartText = ""
        editEndText = ""
        editClipIds = []
    }

    function saveEdit(note) {
        if (!note || editingMarkerId.length === 0 || !activeTimeline)
            return false
        const isRange = String(note.anchorType || "frame") === "range"
        const startSeconds = Number(editStartText)
        const endSeconds = Number(editEndText)
        const validTime = editStartText.trim().length > 0
                         && Number.isFinite(startSeconds) && startSeconds >= 0
                         && startSeconds <= totalDurationSeconds
                         && (!isRange || rangeDraftValid(editStartText, editEndText))
        if (!validTime) {
            statusMessage = "Set a valid note time or range before saving."
            return false
        }
        const startFrame = frameAt(startSeconds)
        const endFrame = isRange ? Math.max(startFrame + 1, frameAt(endSeconds)) : startFrame
        const resolved = note.resolved === true || note.completed === true
        const sourceClipIds = note.clipIds || []
        let sameClipIds = sourceClipIds.length === editClipIds.length
        for (let index = 0; sameClipIds && index < sourceClipIds.length; ++index)
            sameClipIds = String(sourceClipIds[index] || "") === String(editClipIds[index] || "")
        const sameRange = startFrame === Number(note.frame || 0)
                && endFrame === Number(note.endFrame || note.frame || 0)
        const requestedLabel = simplifiedText(editLabel)
        const expectedLabel = requestedLabel.length === 0
                ? String(note.label || "") : requestedLabel
        if (expectedLabel === String(note.label || "")
                && editNote.trim() === String(note.note || "")
                && sameRange && sameClipIds
                && editApprovalState === String(note.approvalState || "open")
                && editAssignee === String(note.assignee || "")
                && resolved === (note.resolved === true || note.completed === true)) {
            statusMessage = "No changes to save."
            cancelEdit()
            return true
        }
        const updated = documentController.updateAnchoredReviewNote(
            editingMarkerId, editLabel, editNote, startFrame, endFrame,
            editClipIds, editAssignee, editApprovalState, resolved)
        if (!updated) {
            statusMessage = String(documentController.status || "No changes were saved.")
            return false
        }
        statusMessage = "Saved review note."
        noteUpdated(editingMarkerId)
        cancelEdit()
        return true
    }

    function toggleResolved(note) {
        if (!note || !activeTimeline)
            return false
        const id = String(note.markerId || "")
        const nextResolved = !(note.resolved === true || note.completed === true)
        if (!documentController.resolveTimelineReviewNote(id, nextResolved)) {
            statusMessage = String(documentController.status || "The note status could not be changed.")
            return false
        }
        statusMessage = nextResolved ? "Marked note resolved." : "Reopened note."
        return true
    }

    function jumpToNote(note) {
        if (!note)
            return false
        const seconds = noteStartSeconds(note)
        jumpRequested(seconds)
        return true
    }

    Connections {
        target: root.documentController
        function onMarkersChanged() {
            if (root.editingMarkerId.length > 0) {
                const current = root.noteForId(root.editingMarkerId)
                if (!current)
                    root.cancelEdit()
            }
        }
    }

    function noteForId(markerId) {
        const source = notes || []
        for (let index = 0; index < source.length; ++index) {
            if (String(source[index].markerId || "") === String(markerId || ""))
                return source[index]
        }
        return null
    }

    ScrollView {
        id: panelScrollView
        objectName: "timelineReviewPanelScrollView"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: panelContent.implicitHeight + root.theme.space24
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        ColumnLayout {
            id: panelContent
            objectName: "timelineReviewPanelScrollContent"
            x: root.theme.space12
            y: root.theme.space12
            width: Math.max(0, panelScrollView.availableWidth - root.theme.space24)
            spacing: root.theme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.space8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: root.theme.space2
                Label {
                    text: "Review notes"
                    color: root.theme.textStrong
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeSection
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: root.activeTimeline
                          ? String(root.openNoteCount) + " open · "
                            + String(root.resolvedNoteCount) + " resolved"
                          : "Open a timeline to review notes."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    elide: Text.ElideRight
                }
            }

            StatusChip {
                text: String((root.notes || []).length)
                tone: root.notes && root.notes.length > 0 ? "info" : "neutral"
                compact: true
                Accessible.name: String((root.notes || []).length) + " review notes"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.space4
            Repeater {
                model: [
                    {"label": "All", "value": "all"},
                    {"label": "Open", "value": "open"},
                    {"label": "Resolved", "value": "resolved"}
                ]
                delegate: FilterChip {
                    required property var modelData
                    objectName: "timelineReviewFilter_" + String(modelData.value)
                    text: modelData.label
                    active: root.filter === modelData.value
                    onClicked: root.setFilter(String(modelData.value))
                }
            }
            Item { Layout.fillWidth: true }
        }

        ListView {
            id: noteList
            objectName: "timelineReviewPanelList"
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.minimumHeight: root.theme.scaled(72)
            Layout.preferredHeight: Math.max(root.theme.scaled(72),
                                               Math.min(root.theme.scaled(150), root.height * 0.28))
            Layout.maximumHeight: Math.max(root.theme.scaled(72),
                                             Math.min(root.theme.scaled(150), root.height * 0.28))
            clip: true
            spacing: root.theme.space6
            model: root.visibleNotes
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }

            delegate: GlassSurface {
                id: noteCard
                required property var modelData
                objectName: "timelineReviewNoteCard-" + String(modelData.markerId || "")
                width: ListView.view.width
                implicitHeight: noteContent.implicitHeight + root.theme.space12 * 2
                height: implicitHeight
                radius: root.theme.radius4
                edgeOpacity: 0.08
                highlightOpacity: 0.04
                readonly property bool isEditing:
                    root.editingMarkerId === String(modelData.markerId || "")
                readonly property bool isResolved:
                    modelData.resolved === true || modelData.completed === true

                ColumnLayout {
                    id: noteContent
                    anchors.fill: parent
                    anchors.margins: root.theme.space10
                    spacing: root.theme.space6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: root.theme.space6

                        Label {
                            Layout.fillWidth: true
                            text: String(modelData.label || "Review note")
                            color: root.theme.textStrong
                            font.family: root.theme.fontUi
                            font.pixelSize: root.theme.typeBody
                            font.weight: Font.DemiBold
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }

                        ActionButton {
                            objectName: "timelineReviewNoteJumpButton-" + String(modelData.markerId || "")
                            text: root.noteAnchorText(modelData)
                            compact: true
                            tone: "secondary"
                            tooltipText: "Jump to this note."
                            onClicked: root.jumpToNote(modelData)
                        }

                        StatusChip {
                            text: root.noteStatus(modelData)
                            tone: root.noteTone(modelData)
                            compact: true
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: String(modelData.note || "").length > 0
                        text: String(modelData.note || "")
                        color: root.theme.textMain
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeLabel
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: (modelData.clipIds || []).length > 0
                        spacing: root.theme.space4
                        AppIcon {
                            name: "link"
                            color: root.theme.textMuted
                            implicitWidth: root.theme.scaled(13)
                            implicitHeight: root.theme.scaled(13)
                        }
                        Label {
                            text: (modelData.clipIds || []).length === 1
                                  ? "Linked to 1 clip"
                                  : "Linked to " + String((modelData.clipIds || []).length) + " clips"
                            color: root.theme.textMuted
                            font.family: root.theme.fontUi
                            font.pixelSize: root.theme.typeCaption
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            visible: String(modelData.assignee || "").length > 0
                            text: String(modelData.assignee || "")
                            color: root.theme.accentCool300
                            font.family: root.theme.fontUi
                            font.pixelSize: root.theme.typeCaption
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: root.theme.space4
                        visible: !noteCard.isEditing

                        ActionButton {
                            objectName: "timelineReviewNoteEditButton-" + String(modelData.markerId || "")
                            text: "Edit"
                            compact: true
                            onClicked: root.beginEdit(modelData)
                        }

                        ActionButton {
                            objectName: "timelineReviewNoteResolveButton-" + String(modelData.markerId || "")
                            text: noteCard.isResolved ? "Reopen" : "Resolve"
                            compact: true
                            active: noteCard.isResolved
                            onClicked: root.toggleResolved(modelData)
                        }
                        Item { Layout.fillWidth: true }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: noteCard.isEditing
                        spacing: root.theme.space6

                        ToneTextField {
                            objectName: "timelineReviewNoteEditLabel"
                            Layout.fillWidth: true
                            text: root.editLabel
                            placeholderText: "Short title"
                            onTextChanged: root.editLabel = text
                        }

                        ToneTextArea {
                            objectName: "timelineReviewNoteEditBody"
                            Layout.fillWidth: true
                            Layout.preferredHeight: root.theme.scaled(74)
                            text: root.editNote
                            placeholderText: "What should change?"
                            onTextChanged: root.editNote = text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: root.theme.space6
                            ToneTextField {
                                objectName: "timelineReviewNoteEditStart"
                                Layout.fillWidth: true
                                numericEntry: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                text: root.editStartText
                                placeholderText: "In · seconds"
                                onTextChanged: root.editStartText = text
                            }
                            ToneTextField {
                                objectName: "timelineReviewNoteEditEnd"
                                Layout.fillWidth: true
                                numericEntry: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                text: root.editEndText
                                placeholderText: "Out · seconds"
                                enabled: String(modelData.anchorType || "frame") === "range"
                                onTextChanged: root.editEndText = text
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            ActionButton {
                                text: "Cancel"
                                compact: true
                                onClicked: root.cancelEdit()
                            }
                            Item { Layout.fillWidth: true }
                            ActionButton {
                                objectName: "timelineReviewNoteSaveButton-" + String(modelData.markerId || "")
                                text: "Save"
                                tone: "primary"
                                compact: true
                                enabled: root.editNote.trim().length > 0
                                         && (String(modelData.anchorType || "frame") === "range"
                                             ? root.rangeDraftValid(root.editStartText, root.editEndText)
                                             : Number.isFinite(Number(root.editStartText))
                                               && Number(root.editStartText) >= 0
                                               && Number(root.editStartText) <= root.totalDurationSeconds)
                                onClicked: root.saveEdit(modelData)
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: root.activeTimeline && root.visibleNotes.length === 0
                text: (root.notes || []).length === 0
                      ? "No review notes yet. Add feedback at the current frame or mark a range."
                      : "No notes match this filter."
                color: root.theme.textMuted
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeLabel
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                width: parent.width - root.theme.space24
            }
        }

        Item {
            Layout.fillWidth: true
            objectName: "timelineReviewComposerSection"
            implicitHeight: composerColumn.implicitHeight

            ColumnLayout {
                id: composerColumn
                anchors.fill: parent
                spacing: root.theme.space6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space4
                    Label {
                        Layout.fillWidth: true
                        text: "Add note"
                        color: root.theme.textStrong
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeBody
                        font.weight: Font.DemiBold
                    }
                    FilterChip {
                        objectName: "timelineReviewAnchorFrame"
                        text: "Frame · " + root.timecode(root.playheadSeconds)
                        active: root.anchorMode === "frame"
                        onClicked: root.anchorMode = "frame"
                    }
                    FilterChip {
                        objectName: "timelineReviewAnchorRange"
                        text: "In–Out"
                        active: root.anchorMode === "range"
                        tooltipText: root.validInOutRange
                                      ? "Use the current In and Out marks."
                                      : "Set a range below; current In/Out marks will be used when available."
                        onClicked: root.beginRangeDraft()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.anchorMode === "range"
                    spacing: root.theme.space6
                    Label {
                        Layout.fillWidth: true
                        text: "Range"
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                    }
                    ActionButton {
                        objectName: "timelineReviewLoadInOutRange"
                        visible: root.validInOutRange
                        text: "Load In–Out"
                        compact: true
                        tooltipText: "Fill the range from the current In and Out marks."
                        onClicked: root.beginRangeDraft()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.anchorMode === "range"
                    spacing: root.theme.space6
                    ToneTextField {
                        id: rangeStartField
                        objectName: "timelineReviewRangeStart"
                        Layout.fillWidth: true
                        numericEntry: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        placeholderText: "In · seconds"
                        text: Math.max(0, Number(root.playheadSeconds || 0)).toFixed(3)
                    }
                    ToneTextField {
                        id: rangeEndField
                        objectName: "timelineReviewRangeEnd"
                        Layout.fillWidth: true
                        numericEntry: true
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        placeholderText: "Out · seconds"
                        text: Math.min(root.totalDurationSeconds,
                                       Number(root.playheadSeconds || 0) + 5).toFixed(3)
                    }
                }

                ToneTextField {
                    objectName: "timelineReviewNoteTitle"
                    Layout.fillWidth: true
                    text: root.draftLabel
                    placeholderText: "Short title · optional"
                    onTextChanged: root.draftLabel = text
                }

                ToneTextArea {
                    objectName: "timelineReviewNoteComposer"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.theme.scaled(74)
                    text: root.draftNote
                    placeholderText: "What should change?"
                    enabled: root.activeTimeline
                    onTextChanged: root.draftNote = text
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space6
                    ToneCheckBox {
                        objectName: "timelineReviewIncludeClipContext"
                        visible: root.currentClipId.length > 0
                        enabled: root.activeTimeline
                        checked: root.includeCurrentClip
                        text: "Link selected clip"
                        onToggled: root.includeCurrentClip = checked
                        Accessible.name: "Link review note to selected clip"
                    }
                    Label {
                        objectName: "timelineReviewClipContextLabel"
                        Layout.fillWidth: true
                        visible: root.includeCurrentClip && root.currentClipLabel.length > 0
                        text: root.currentClipLabel
                        color: root.theme.textMuted
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.theme.space6
                    Label {
                        objectName: "timelineReviewPanelStatus"
                        Layout.fillWidth: true
                        text: root.statusMessage
                        color: root.statusMessage.length > 0 ? root.theme.textMuted : "transparent"
                        font.family: root.theme.fontUi
                        font.pixelSize: root.theme.typeCaption
                        elide: Text.ElideRight
                    }
                    ActionButton {
                        objectName: "timelineReviewAddNoteButton"
                        text: "Add note"
                        iconName: "plus"
                        tone: "primary"
                        compact: true
                        enabled: root.activeTimeline && root.draftNote.trim().length > 0
                                 && (root.anchorMode !== "range"
                                     || root.rangeDraftValid(rangeStartField.text,
                                                            rangeEndField.text))
                        disabledReason: !root.activeTimeline
                                        ? "Open a timeline to add a review note."
                                        : root.draftNote.trim().length === 0
                                          ? "Write a note before adding it."
                                          : "Set Out after In and within the sequence duration."
                        onClicked: root.createNote()
                    }
                }
            }
        }
    }
}
}
