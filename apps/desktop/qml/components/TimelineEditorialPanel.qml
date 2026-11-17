pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

GlassSurface {
    id: root

    required property var theme
    required property var documentController
    required property var currentClip
    required property string kind
    property var titleTemplates: null
    property var transcriptSegments: null
    property string transcriptSearchText: ""
    property string transcriptMonitor: "program"
    property string selectedTranscriptSegmentId: ""
    property int selectedTranscriptWordIndex: -1
    property string transcriptEditText: ""
    property string transcriptEditSpeaker: ""
    property string statusMessage: ""
    property var captionCueItems: []
    property var retainedCaptionDraftByClipId: ({})
    property var sequenceTrackSnapshot: []

    readonly property string timelineEditorialPanelContract:
        "timeline-editorial-panel:v1|titles:controller-templates-and-editable-shapes|captions:cue-list-timecode-seek-controller-edit|transcript:search-word-seek-correction-controller-edit|undo:document-controller|guards:timeline-selection-track-lock-empty-model|deep-editors:openEditorRequested"
    readonly property var effectiveTitleTemplates: titleTemplates !== null
        ? titleTemplates : (documentController ? documentController.titleTemplates : [])
    readonly property var effectiveTranscriptSegments: transcriptSegments !== null
        ? transcriptSegments : (documentController ? documentController.transcriptSegments : [])
    readonly property var captionCues: captionCueItems
    readonly property bool timelineAvailable: documentController && documentController.hasActiveTimeline === true
    readonly property bool canAddGraphic: {
        if (!timelineAvailable || !documentController)
            return false
        const tracks = sequenceTrackSnapshot || []
        for (let index = 0; index < tracks.length; ++index) {
            const track = tracks[index]
            if (String(track.trackRole || "") === "graphics" && track.locked === true)
                return false
        }
        return true
    }
    readonly property bool hasSelectedTranscriptWord:
        selectedTranscriptSegmentId.length > 0 && selectedTranscriptWordIndex >= 0 &&
        selectedTranscriptWord() !== null
    readonly property bool canEditSelectedTranscriptWord:
        hasSelectedTranscriptWord && timelineAvailable &&
        !transcriptTrackLocked(selectedTranscriptSegmentId)
    readonly property int matchingTranscriptSegmentCount: {
        const segments = effectiveTranscriptSegments || []
        let count = 0
        for (let index = 0; index < segments.length; ++index) {
            if (segmentMatchesSearch(segments[index]))
                ++count
        }
        return count
    }

    signal openEditorRequested(string kind)
    signal titleAdded(string clipId)
    signal graphicAdded(string clipId)
    signal captionEdited(string clipId)
    signal transcriptWordEdited(string segmentId, int wordIndex)

    Connections {
        target: root.documentController
        function onClipsChanged() {
            root.refreshCaptionCues()
            const word = root.selectedTranscriptWord()
            if (word) {
                root.transcriptEditText = String(word.text || "")
                root.transcriptEditSpeaker = String(word.speaker || "")
            } else if (root.selectedTranscriptSegmentId.length > 0) {
                root.selectedTranscriptSegmentId = ""
                root.selectedTranscriptWordIndex = -1
                root.transcriptEditText = ""
                root.transcriptEditSpeaker = ""
            }
        }
        function onSequenceChanged() {
            root.refreshSequenceTrackSnapshot()
        }
    }

    Component.onCompleted: {
        refreshCaptionCues()
        refreshSequenceTrackSnapshot()
    }
    onDocumentControllerChanged: {
        refreshCaptionCues()
        refreshSequenceTrackSnapshot()
    }

    edgeOpacity: 0.12
    highlightOpacity: 0.08
    Accessible.role: Accessible.Pane
    Accessible.name: kind === "titles" ? "Titles and graphics"
                     : kind === "captions" ? "Caption cues" : "Transcript"

    function trackLocked(trackId) {
        if (!documentController || !trackId)
            return true
        const tracks = sequenceTrackSnapshot || []
        for (let index = 0; index < tracks.length; ++index) {
            if (String(tracks[index].trackId || "") === String(trackId))
                return tracks[index].locked === true
        }
        return true
    }

    function refreshCaptionCues() {
        captionCueItems = documentController
                ? documentController.captionCueSnapshot() : []
    }

    function refreshSequenceTrackSnapshot() {
        sequenceTrackSnapshot = documentController
                ? documentController.snapshotSequenceTracks() : []
    }

    function rememberCaptionDraft(clip, text) {
        const clipId = String(clip && clip.clipId || "")
        if (clipId.length === 0)
            return
        const drafts = Object.assign({}, retainedCaptionDraftByClipId || ({}))
        const draft = String(text || "")
        if (draft === String(clip.titleText || ""))
            delete drafts[clipId]
        else
            drafts[clipId] = draft
        retainedCaptionDraftByClipId = drafts
    }

    function clearCaptionDraft(clipId) {
        const key = String(clipId || "")
        if (key.length === 0 || retainedCaptionDraftByClipId[key] === undefined)
            return
        const drafts = Object.assign({}, retainedCaptionDraftByClipId || ({}))
        delete drafts[key]
        retainedCaptionDraftByClipId = drafts
    }

    function captionDraftForClip(clip) {
        const clipId = String(clip && clip.clipId || "")
        const retained = (retainedCaptionDraftByClipId || ({}))[clipId]
        return retained === undefined ? String(clip && clip.titleText || "") : String(retained)
    }

    function canEditCaption(clip) {
        return timelineAvailable && clip && String(clip.mediaType || "") === "caption" &&
                Number(clip.index) >= 0 && !trackLocked(clip.trackId)
    }

    function addTitleTemplate(templateKey) {
        const key = String(templateKey || "").trim()
        if (!canAddGraphic || key.length === 0 || !documentController.addTitleClipFromTemplate(key))
            return false
        const clip = documentController.currentClip || ({})
        titleAdded(String(clip.clipId || ""))
        statusMessage = "Added " + String(clip.displayLabel || "title") + " at the playhead."
        return true
    }

    function addGraphicShape(shapeType) {
        const shape = String(shapeType || "").trim()
        if (!canAddGraphic || ["rectangle", "rounded-rectangle", "ellipse"].indexOf(shape) < 0 ||
                !documentController.addGraphicShape(shape))
            return false
        const clip = documentController.currentClip || ({})
        graphicAdded(String(clip.clipId || ""))
        statusMessage = "Added an editable " + String(clip.displayLabel || shape) + "."
        return true
    }

    function trackTimecode(seconds) {
        const safe = Math.max(0, Number(seconds || 0))
        const fps = Math.max(1, Math.round(Number(documentController.framesPerSecond || 30)))
        const totalFrames = Math.floor(safe * fps)
        const hours = Math.floor(totalFrames / (3600 * fps))
        const minutes = Math.floor((totalFrames % (3600 * fps)) / (60 * fps))
        const remainingSeconds = Math.floor((totalFrames % (60 * fps)) / fps)
        const frames = totalFrames % fps
        return String(hours).padStart(2, "0") + ":" + String(minutes).padStart(2, "0") + ":" +
                String(remainingSeconds).padStart(2, "0") + ":" + String(frames).padStart(2, "0")
    }

    function selectAndSeekCaption(clip) {
        if (!documentController || !clip || Number(clip.index) < 0)
            return false
        documentController.selectClip(Number(clip.index))
        documentController.seekSequenceSeconds(Number(clip.startSeconds || 0))
        return true
    }

    function commitCaptionText(clip, text) {
        if (!canEditCaption(clip)) {
            statusMessage = "Unlock the caption track before editing its cues."
            return false
        }
        const nextText = String(text || "").trim().slice(0, 500)
        if (nextText.length === 0) {
            statusMessage = "Caption text cannot be empty."
            return false
        }
        const selected = documentController.currentClip || ({})
        if (String(selected.clipId || "") === String(clip.clipId || "") &&
                String(selected.titleText || "").trim() === nextText)
            return true
        documentController.selectClip(Number(clip.index))
        documentController.updateSelectedTitle({ "text": nextText })
        const updatedSelection = documentController.currentClip || ({})
        const updated = String(updatedSelection.clipId || "") === String(clip.clipId || "") &&
                String(updatedSelection.titleText || "").trim() === nextText
        if (updated) {
            statusMessage = "Updated caption at " + trackTimecode(clip.startSeconds) + "."
            captionEdited(String(clip.clipId || ""))
        }
        return updated
    }

    function segmentMatchesSearch(segment) {
        const query = transcriptSearchText.trim().toLowerCase()
        if (query.length === 0)
            return true
        return (String(segment.text || "") + " " + String(segment.speaker || ""))
                .toLowerCase().indexOf(query) >= 0
    }

    function segmentForId(segmentId) {
        const segments = effectiveTranscriptSegments || []
        for (let index = 0; index < segments.length; ++index) {
            if (String(segments[index].segmentId || "") === String(segmentId || ""))
                return segments[index]
        }
        return null
    }

    function transcriptSegmentClip(segmentId) {
        if (!documentController || String(segmentId || "").length === 0)
            return null
        const result = documentController.inspectTimelineClipById(String(segmentId))
        return result && result.ok === true ? result.clip : null
    }

    function transcriptTrackLocked(segmentId) {
        const clip = transcriptSegmentClip(segmentId)
        return !clip || trackLocked(clip.trackId)
    }

    function selectedTranscriptWord() {
        const segment = segmentForId(selectedTranscriptSegmentId)
        const words = segment && segment.words ? segment.words : []
        for (let index = 0; index < words.length; ++index) {
            if (Number(words[index].index) === selectedTranscriptWordIndex)
                return words[index]
        }
        return null
    }

    function chooseTranscriptWord(segment, word) {
        const segmentId = String(segment && segment.segmentId || "")
        if (!word)
            return false
        const wordIndex = Number(word.index === undefined ? -1 : word.index)
        if (segmentId.length === 0 || wordIndex < 0)
            return false
        selectedTranscriptSegmentId = segmentId
        selectedTranscriptWordIndex = wordIndex
        transcriptEditText = String(word.text || "")
        transcriptEditSpeaker = String(word.speaker || segment.speaker || "")
        const sought = documentController.seekTranscriptWord(segmentId, wordIndex, transcriptMonitor)
        statusMessage = sought ? "Moved " + transcriptMonitor.toUpperCase() + " to " +
                        trackTimecode(word.startSeconds) + "."
                      : String(documentController.status || "The selected word could not be reached.")
        return sought
    }

    function seekSelectedTranscriptWord(monitor) {
        if (!hasSelectedTranscriptWord || !documentController)
            return false
        transcriptMonitor = String(monitor || "program").toLowerCase() === "source" ? "source" : "program"
        const sought = documentController.seekTranscriptWord(
                    selectedTranscriptSegmentId, selectedTranscriptWordIndex, transcriptMonitor)
        statusMessage = sought ? "Moved " + transcriptMonitor.toUpperCase() + " to the selected word."
                               : String(documentController.status || "The selected word could not be reached.")
        return sought
    }

    function saveTranscriptWordCorrection() {
        const word = selectedTranscriptWord()
        if (!word || !documentController || transcriptEditText.trim().length === 0) {
            statusMessage = "Choose a timed transcript word and enter its correction."
            return false
        }
        if (!canEditSelectedTranscriptWord) {
            statusMessage = "Unlock the caption track before correcting transcript words."
            return false
        }
        const segmentId = selectedTranscriptSegmentId
        const wordIndex = selectedTranscriptWordIndex
        const updated = documentController.updateTranscriptWord(
                    segmentId, wordIndex, transcriptEditText, transcriptEditSpeaker)
        if (updated) {
            transcriptEditText = transcriptEditText.trim()
            transcriptEditSpeaker = transcriptEditSpeaker.trim()
            statusMessage = "Saved transcript correction."
            transcriptWordEdited(segmentId, wordIndex)
        } else {
            statusMessage = String(documentController.status || "Transcript correction was not saved.")
        }
        return updated
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.theme.space10
        spacing: root.theme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.space6

            ColumnLayout {
                Layout.fillWidth: true
                spacing: root.theme.space2

                Label {
                    objectName: "timelineEditorialPanelTitle"
                    text: root.kind === "titles" ? "Titles & graphics"
                          : root.kind === "captions" ? "Caption cues" : "Transcript"
                    color: root.theme.textStrong
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeSection
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: root.kind === "titles" ? "Editable overlays at the playhead"
                          : root.kind === "captions" ? String(root.captionCues.length) + " timed cues"
                          : String(root.effectiveTranscriptSegments.length) + " searchable segments"
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }
            }

            ActionButton {
                objectName: "timelineEditorialUndoButton"
                visible: root.kind !== "titles"
                text: "Undo"
                iconName: "undo"
                compact: true
                tone: "quiet"
                enabled: root.documentController && root.documentController.canUndo
                disabledReason: "There is no edit to undo."
                tooltipText: "Undo the latest timeline edit"
                onClicked: root.documentController.undo()
            }
        }

        Loader {
            id: contentLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: true
            sourceComponent: root.kind === "titles" ? titlesContent
                             : root.kind === "captions" ? captionsContent
                             : root.kind === "transcript" ? transcriptContent : unsupportedContent
        }

        Label {
            objectName: "timelineEditorialPanelStatus"
            visible: root.statusMessage.length > 0
            Layout.fillWidth: true
            text: root.statusMessage
            color: root.theme.textMuted
            font.family: root.theme.fontUi
            font.pixelSize: root.theme.typeCaption
            wrapMode: Text.Wrap
        }
    }

    Component {
        id: titlesContent
        ColumnLayout {
            spacing: root.theme.space8

            Label {
                Layout.fillWidth: true
                visible: !root.timelineAvailable
                text: "Create a timeline before adding a title or graphic."
                color: root.theme.textMuted
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeBody
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: "Title templates"
                color: root.theme.textMuted
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeLabel
                font.weight: Font.DemiBold
            }

            ListView {
                id: titleTemplateList
                objectName: "timelineEditorialTitleTemplateList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: root.theme.space6
                model: root.effectiveTitleTemplates
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: templateCard
                    required property var modelData
                    width: titleTemplateList.width
                    implicitHeight: templateCardLayout.implicitHeight + root.theme.space10 * 2
                    radius: root.theme.radiusSurface
                    color: root.theme.alpha(root.theme.ink700, 0.30)
                    border.width: 0

                    RowLayout {
                        id: templateCardLayout
                        anchors.fill: parent
                        anchors.margins: root.theme.space10
                        spacing: root.theme.space8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: root.theme.space2

                            Label {
                                text: String(templateCard.modelData.category || "TITLE").toUpperCase()
                                color: root.theme.accentCool300
                                font.family: root.theme.fontNumeric
                                font.features: root.theme.numericFontFeatures
                                font.pixelSize: root.theme.typeCaption
                                font.weight: Font.DemiBold
                            }

                            Label {
                                Layout.fillWidth: true
                                text: String(templateCard.modelData.label || "Title")
                                color: root.theme.textStrong
                                font.family: root.theme.fontUi
                                font.pixelSize: root.theme.typeLabel
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: String(templateCard.modelData.description || "")
                                color: root.theme.textMuted
                                font.family: root.theme.fontUi
                                font.pixelSize: root.theme.typeCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        ActionButton {
                            objectName: "timelineEditorialAddTitle_" + String(templateCard.modelData.key || "")
                            text: "Add"
                            iconName: "plus"
                            compact: true
                            enabled: root.canAddGraphic
                            disabledReason: "Create a timeline and unlock any graphics track before adding titles."
                            onClicked: root.addTitleTemplate(String(templateCard.modelData.key || ""))
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: root.effectiveTitleTemplates.length === 0
                    text: "No title templates are available."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeBody
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: root.theme.space6

                Label {
                    text: "Graphic shapes"
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeLabel
                    font.weight: Font.DemiBold
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: root.theme.gapDense

                    ActionButton {
                        objectName: "timelineEditorialAddRectangle"
                        text: "Rectangle"
                        iconName: "shape"
                        compact: true
                        enabled: root.canAddGraphic
                        disabledReason: "Create a timeline and unlock any graphics track before adding shapes."
                        onClicked: root.addGraphicShape("rectangle")
                    }

                    ActionButton {
                        objectName: "timelineEditorialAddRoundedRectangle"
                        text: "Rounded"
                        iconName: "shape"
                        compact: true
                        enabled: root.canAddGraphic
                        disabledReason: "Create a timeline and unlock any graphics track before adding shapes."
                        onClicked: root.addGraphicShape("rounded-rectangle")
                    }

                    ActionButton {
                        objectName: "timelineEditorialAddEllipse"
                        text: "Ellipse"
                        iconName: "shape"
                        compact: true
                        enabled: root.canAddGraphic
                        disabledReason: "Create a timeline and unlock any graphics track before adding shapes."
                        onClicked: root.addGraphicShape("ellipse")
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    objectName: "timelineEditorialOpenTitlePicker"
                    text: "Full title editor"
                    iconName: "external-link"
                    compact: true
                    tone: "quiet"
                    enabled: root.timelineAvailable
                    onClicked: root.openEditorRequested("titles")
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    Component {
        id: captionsContent
        ColumnLayout {
            spacing: root.theme.space8

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.timelineAvailable
                          ? "Select a cue to seek, then edit its text. Changes use timeline undo."
                          : "Create a timeline to work with caption cues."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    wrapMode: Text.Wrap
                }

                ActionButton {
                    objectName: "timelineEditorialOpenCaptionTools"
                    text: "Caption tools"
                    iconName: "captions"
                    compact: true
                    tone: "quiet"
                    enabled: root.timelineAvailable
                    onClicked: root.openEditorRequested("captions")
                }
            }

            ListView {
                id: captionCueList
                objectName: "timelineEditorialCaptionCueList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: root.theme.space6
                model: root.captionCues
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: captionCueCard
                    required property var modelData
                    required property int index
                    property string draftText: root.captionDraftForClip(modelData)
                    width: captionCueList.width
                    implicitHeight: captionCueLayout.implicitHeight + root.theme.space10 * 2
                    radius: root.theme.radiusSurface
                    color: String(root.currentClip.clipId || "") === String(modelData.clipId || "")
                           ? root.theme.alpha(root.theme.editorSelectionFill, 0.68)
                           : root.theme.alpha(root.theme.ink700, 0.30)
                    border.width: 0
                    onDraftTextChanged: root.rememberCaptionDraft(modelData, draftText)
                    onModelDataChanged: draftText = root.captionDraftForClip(modelData)

                    ColumnLayout {
                        id: captionCueLayout
                        anchors.fill: parent
                        anchors.margins: root.theme.space10
                        spacing: root.theme.space6

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                Layout.fillWidth: true
                                text: root.trackTimecode(captionCueCard.modelData.startSeconds) + "  –  " +
                                      root.trackTimecode(captionCueCard.modelData.endSeconds)
                                color: root.theme.textStrong
                                font.family: root.theme.fontNumeric
                                font.features: root.theme.numericFontFeatures
                                font.pixelSize: root.theme.typeCaption
                            }

                            ActionButton {
                                objectName: "timelineEditorialSeekCaption_" + String(captionCueCard.modelData.clipId || "")
                                text: "Go to cue"
                                iconName: "play"
                                compact: true
                                tone: "quiet"
                                enabled: root.timelineAvailable
                                onClicked: root.selectAndSeekCaption(captionCueCard.modelData)
                            }
                        }

                        ToneTextField {
                            objectName: "timelineEditorialCaptionText_" + String(captionCueCard.modelData.clipId || "")
                            Layout.fillWidth: true
                            text: captionCueCard.draftText
                            accessibleLabel: "Caption text at " + root.trackTimecode(captionCueCard.modelData.startSeconds)
                            placeholderText: "Caption text"
                            enabled: root.canEditCaption(captionCueCard.modelData)
                            onTextEdited: captionCueCard.draftText = text
                            function commitDraft() {
                                if (root.commitCaptionText(captionCueCard.modelData, captionCueCard.draftText)) {
                                    root.clearCaptionDraft(String(captionCueCard.modelData.clipId || ""))
                                    captionCueCard.draftText = String(root.documentController.currentClip.titleText || "")
                                }
                            }
                            onAccepted: commitDraft()
                            onEditingFinished: {
                                if (captionCueCard.draftText.trim() !==
                                        String(captionCueCard.modelData.titleText || "").trim())
                                    commitDraft()
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                Layout.fillWidth: true
                                text: root.trackLocked(captionCueCard.modelData.trackId)
                                      ? "Track locked" : "Cue " + String(captionCueCard.index + 1)
                                color: root.trackLocked(captionCueCard.modelData.trackId)
                                       ? root.theme.warning : root.theme.textFaint
                                font.family: root.theme.fontUi
                                font.pixelSize: root.theme.typeCaption
                            }

                            ActionButton {
                                objectName: "timelineEditorialSaveCaption_" + String(captionCueCard.modelData.clipId || "")
                                text: "Save cue"
                                iconName: "check"
                                compact: true
                                enabled: root.canEditCaption(captionCueCard.modelData) &&
                                         captionCueCard.draftText.trim().length > 0 &&
                                         captionCueCard.draftText.trim() !== String(captionCueCard.modelData.titleText || "").trim()
                                disabledReason: root.trackLocked(captionCueCard.modelData.trackId)
                                    ? "Unlock the caption track to edit this cue."
                                    : "Enter changed, non-empty text to save the cue."
                                onClicked: {
                                    if (root.commitCaptionText(captionCueCard.modelData, captionCueCard.draftText)) {
                                        root.clearCaptionDraft(String(captionCueCard.modelData.clipId || ""))
                                        captionCueCard.draftText = String(root.documentController.currentClip.titleText || "")
                                    }
                                }
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: root.captionCues.length === 0
                    width: Math.min(parent.width - root.theme.space16, root.theme.scaled(360))
                    text: root.timelineAvailable
                          ? "No caption cues yet. Use Caption tools to transcribe audio or generate captions from a transcript."
                          : "No active timeline."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeBody
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: root.theme.space4

                Label {
                    Layout.fillWidth: true
                    text: root.documentController ? String(root.documentController.status || "") : ""
                    color: root.theme.textFaint
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeCaption
                    elide: Text.ElideRight
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: root.theme.gapDense
                    flow: Flow.LeftToRight

                    ActionButton {
                        objectName: "timelineEditorialOpenCaptionStyles"
                        text: "Styles & validation"
                        iconName: "adjust"
                        compact: true
                        tone: "quiet"
                        enabled: root.timelineAvailable && Number(root.documentController.captionCount || 0) > 0
                        onClicked: root.openEditorRequested("caption-styles")
                    }
                    ActionButton {
                        objectName: "timelineEditorialOpenCaptionGenerator"
                        text: "Generate"
                        iconName: "captions"
                        compact: true
                        enabled: root.timelineAvailable
                        onClicked: root.openEditorRequested("caption-generator")
                    }
                }
            }
        }
    }

    Component {
        id: transcriptContent
        ColumnLayout {
            spacing: root.theme.space8

            RowLayout {
                Layout.fillWidth: true

                ToneTextField {
                    objectName: "timelineEditorialTranscriptSearch"
                    Layout.fillWidth: true
                    text: root.transcriptSearchText
                    placeholderText: "Search words and speakers"
                    onTextEdited: root.transcriptSearchText = text
                }

                ActionButton {
                    objectName: "timelineEditorialOpenTranscriptEditor"
                    text: "Full editor"
                    iconName: "external-link"
                    compact: true
                    tone: "quiet"
                    enabled: root.timelineAvailable
                    onClicked: root.openEditorRequested("transcript")
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.transcriptMonitor === "source"
                      ? "Click a timed word to move Source; corrections are undoable."
                      : "Click a timed word to move Program; corrections are undoable."
                color: root.theme.textMuted
                font.family: root.theme.fontUi
                font.pixelSize: root.theme.typeCaption
                wrapMode: Text.Wrap
            }

            ListView {
                id: transcriptSegmentList
                objectName: "timelineEditorialTranscriptSegmentList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: root.theme.space6
                model: root.effectiveTranscriptSegments
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: transcriptSegmentCard
                    required property var modelData
                    required property int index
                    readonly property bool matchesSearch: root.segmentMatchesSearch(modelData)
                    width: transcriptSegmentList.width
                    height: matchesSearch ? segmentLayout.implicitHeight + root.theme.space10 * 2 : 0
                    visible: matchesSearch
                    radius: root.theme.radiusSurface
                    color: root.selectedTranscriptSegmentId === String(modelData.segmentId || "")
                           ? root.theme.alpha(root.theme.editorSelectionFill, 0.68)
                           : root.theme.alpha(root.theme.ink700, 0.30)
                    border.width: 0

                    ColumnLayout {
                        id: segmentLayout
                        anchors.fill: parent
                        anchors.margins: root.theme.space10
                        spacing: root.theme.space6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: root.theme.space6

                            Label {
                                Layout.fillWidth: true
                                text: String(transcriptSegmentCard.modelData.speaker || "Speaker")
                                      + " · Segment " + String(transcriptSegmentCard.index + 1)
                                color: root.theme.textStrong
                                font.family: root.theme.fontUi
                                font.pixelSize: root.theme.typeLabel
                                font.weight: Font.DemiBold
                            }

                            Label {
                                text: root.trackTimecode(transcriptSegmentCard.modelData.startSeconds) +
                                      " – " + root.trackTimecode(transcriptSegmentCard.modelData.endSeconds)
                                color: root.theme.textMuted
                                font.family: root.theme.fontNumeric
                                font.features: root.theme.numericFontFeatures
                                font.pixelSize: root.theme.typeCaption
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: root.theme.space4

                            Repeater {
                                model: transcriptSegmentCard.modelData.words || []

                                delegate: FilterChip {
                                    id: transcriptWordChip
                                    required property var modelData
                                    objectName: "timelineEditorialTranscriptWord_" +
                                                String(transcriptSegmentCard.modelData.segmentId || "") + "_" +
                                                String(modelData.index)
                                    text: String(modelData.text || "")
                                    active: root.selectedTranscriptSegmentId ===
                                            String(transcriptSegmentCard.modelData.segmentId || "") &&
                                            root.selectedTranscriptWordIndex === Number(modelData.index)
                                    tooltipText: "Seek to " + String(modelData.text || "")
                                                 + " at " + root.trackTimecode(modelData.startSeconds)
                                    onClicked: root.chooseTranscriptWord(
                                                   transcriptSegmentCard.modelData, modelData)
                                }
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: root.effectiveTranscriptSegments.length === 0
                    width: Math.min(parent.width - root.theme.space16, root.theme.scaled(360))
                    text: "No timed transcript yet. Generate captions from timeline audio to create searchable words."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeBody
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Label {
                    anchors.centerIn: parent
                    visible: root.effectiveTranscriptSegments.length > 0 &&
                             root.matchingTranscriptSegmentCount === 0
                    width: Math.min(parent.width - root.theme.space16, root.theme.scaled(360))
                    text: "No transcript segments match this search."
                    color: root.theme.textMuted
                    font.family: root.theme.fontUi
                    font.pixelSize: root.theme.typeBody
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }

            Rectangle {
                objectName: "timelineEditorialTranscriptCorrection"
                Layout.fillWidth: true
                visible: root.hasSelectedTranscriptWord
                implicitHeight: correctionLayout.implicitHeight + root.theme.space10 * 2
                radius: root.theme.radiusSurface
                color: root.theme.alpha(root.theme.ink800, 0.58)
                border.width: 0

                ColumnLayout {
                    id: correctionLayout
                    anchors.fill: parent
                    anchors.margins: root.theme.space10
                    spacing: root.theme.space6

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: "Correct selected word"
                            color: root.theme.textStrong
                            font.family: root.theme.fontUi
                            font.pixelSize: root.theme.typeLabel
                            font.weight: Font.DemiBold
                        }

                        ActionButton {
                            objectName: "timelineEditorialSeekProgramWord"
                            text: "Program"
                            iconName: "play"
                            compact: true
                            tone: root.transcriptMonitor === "program" ? "secondary" : "quiet"
                            active: root.transcriptMonitor === "program"
                            onClicked: root.seekSelectedTranscriptWord("program")
                        }

                        ActionButton {
                            objectName: "timelineEditorialSeekSourceWord"
                            text: "Source"
                            iconName: "source"
                            compact: true
                            tone: root.transcriptMonitor === "source" ? "secondary" : "quiet"
                            active: root.transcriptMonitor === "source"
                            onClicked: root.seekSelectedTranscriptWord("source")
                        }
                    }

                    ToneTextField {
                        objectName: "timelineEditorialTranscriptWordEditor"
                        Layout.fillWidth: true
                        text: root.transcriptEditText
                        placeholderText: "Correct word"
                        enabled: root.canEditSelectedTranscriptWord
                        onTextEdited: root.transcriptEditText = text
                        onAccepted: root.saveTranscriptWordCorrection()
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ToneTextField {
                            objectName: "timelineEditorialTranscriptSpeakerEditor"
                            Layout.fillWidth: true
                            text: root.transcriptEditSpeaker
                            placeholderText: "Speaker"
                            enabled: root.canEditSelectedTranscriptWord
                            onTextEdited: root.transcriptEditSpeaker = text
                            onAccepted: root.saveTranscriptWordCorrection()
                        }

                        ActionButton {
                            objectName: "timelineEditorialSaveTranscriptWord"
                            text: "Save correction"
                            iconName: "check"
                            compact: true
                            enabled: root.canEditSelectedTranscriptWord &&
                                     root.selectedTranscriptWord() !== null &&
                                     root.transcriptEditText.trim().length > 0
                            disabledReason: "Choose an available timed word and enter non-empty text."
                            onClicked: root.saveTranscriptWordCorrection()
                        }
                    }
                }
            }
        }
    }

    Component {
        id: unsupportedContent
        Label {
            anchors.centerIn: parent
            text: "This editorial panel is unavailable."
            color: root.theme.textMuted
            font.family: root.theme.fontUi
            font.pixelSize: root.theme.typeBody
        }
    }
}
