pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    objectName: "agentRunDialog"
    visible: false
    focus: visible

    property var agentEditController: null
    property var catalogModel: null
    property var confirmedTargetIds: []
    property string initialScopeMode: "current-photo"
    property string scopeMode: initialScopeMode.length > 0 ? initialScopeMode : "current-photo"
    property var thinkingLines: []
    property var proofImages: []
    property int currentPage: 0
    property bool detailsExpanded: false
    property int runElapsedSeconds: 0
    property string styleGalleryFilter: "all"
    property string styleGalleryQuery: ""
    property string inspirationMode: "preset"
    property var inspirationAssetIds: []
    property string preparedInspirationSignature: ""
    property string directionError: ""
    property string applyError: ""
    property string sampleFeedback: ""
    property bool revisionExpanded: false
    property string alternativeDirectionText: ""
    property bool comparisonPromptOpen: false
    property int activeReviewSampleIndex: 0
    property bool editCommitted: false
    property var committedAssetIds: []
    property var inspirationSuggestions: []
    property var selectedSuggestionKeys: []
    property string suggestionSummary: ""
    property string styleStartMode: "project"
    property string styleStartMessage: ""
    property var memoryVaultSnapshot: ({})
    property string detailsActionMessage: ""
    property bool storyPreferenceForgetArmed: false
    property string storyPreferenceActionMessage: ""
    property real presentationProgress: 0
    property bool closingAfterTransition: false
    readonly property string aiEditUxContract: "ai-edit:v9|entry:left-rail-agent-terminal|sequence:direction-inspiration-live-work-review|surface:inset-retro-compute-panel|motion:scale-fade-slide|target-selection:workspace-context-first|style-start:three-path-project-sticky|scope-override:details|inspiration:visual-selection|activity:live-editor-journey|result:full-resolution-history-backed|details:progressive-disclosure"

    signal scopeModeRequested(string scopeMode)
    signal runRequested()
    signal editApplied(var assetIds)
    signal targetSelectionRequested(string prompt, var inspirationAssetIds, string styleId)
    signal closed()

    Theme { id: theme }

    Timer {
        interval: 1000
        repeat: true
        running: root.agentRunning()
        onTriggered: root.runElapsedSeconds += 1
    }

    onCurrentPageChanged: {
        bubbleReplyAnimation.restart()
        if (currentPage !== 3)
            root.revisionExpanded = false
    }
    onScopeModeChanged: targetChangeAnimation.restart()

    NumberAnimation {
        id: presentationAnimation
        target: root
        property: "presentationProgress"
        to: root.closingAfterTransition ? 0 : 1
        duration: theme.reducedMotion ? 0 : 420
        easing.type: Easing.OutCubic
        onFinished: {
            if (root.closingAfterTransition) {
                root.visible = false
                root.closingAfterTransition = false
                root.closed()
            }
        }
    }

    ParallelAnimation {
        id: bubbleReplyAnimation
        NumberAnimation {
            target: conversationView
            property: "scale"
            from: 0.975
            to: 1
            duration: theme.durationSlow
            easing.type: Easing.OutBack
            easing.overshoot: 0.55
        }
        NumberAnimation {
            target: conversationView
            property: "opacity"
            from: 0.72
            to: 1
            duration: theme.durationSlow
            easing.type: Easing.OutBack
            easing.overshoot: 0.72
        }
    }

    SequentialAnimation {
        id: targetChangeAnimation
        NumberAnimation {
            target: conversationView
            property: "scale"
            from: 0.985
            to: 1
            duration: theme.durationMedium
            easing.type: Easing.OutCubic
        }
    }

    implicitWidth: 1120
    implicitHeight: 820
    Accessible.role: Accessible.Pane
    Accessible.name: "Looksy Agent Studio"

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape && !root.agentRunning()) {
            root.close()
            event.accepted = true
        }
    }

    function open() {
        root.closingAfterTransition = false
        root.presentationProgress = 0
        root.visible = true
        presentationAnimation.restart()
        root.detailsExpanded = false
        root.directionError = ""
        root.applyError = ""
        root.sampleFeedback = ""
        root.revisionExpanded = false
        root.alternativeDirectionText = ""
        root.comparisonPromptOpen = false
        root.activeReviewSampleIndex = 0
        root.inspirationMode = "preset"
        root.inspirationAssetIds = []
        root.preparedInspirationSignature = ""
        root.editCommitted = false
        root.committedAssetIds = []
        root.inspirationSuggestions = []
        root.selectedSuggestionKeys = []
        root.suggestionSummary = ""
        root.styleStartMode = "project"
        root.styleStartMessage = ""
        root.memoryVaultSnapshot = ({})
        root.detailsActionMessage = ""
        root.storyPreferenceForgetArmed = false
        root.storyPreferenceActionMessage = ""
        root.scopeMode = root.bestScope(root.initialScopeMode)
        root.scopeModeRequested(root.scopeMode)
        root.setPage(root.agentRunning() ? 2 : (root.canReviewDraft() ? 3 : 0))
        if (root.hasController())
            root.agentEditController.probeSelectedModelAsync()
        root.forceActiveFocus()
    }

    function close() {
        if (!root.visible || root.agentRunning())
            return
        root.closingAfterTransition = true
        presentationAnimation.restart()
    }

    Connections {
        target: root.agentEditController
        function onRunningChanged() {
            if (root.visible && root.agentRunning()) {
                root.runElapsedSeconds = 0
                root.setPage(2)
            }
        }
        function onDraftChanged() {
            if (root.visible && !root.agentRunning() && root.canReviewDraft())
                root.setPage(3)
        }
    }

    function hasController() {
        return root.agentEditController !== null && root.agentEditController !== undefined
    }

    function hasCatalog() {
        return root.catalogModel !== null && root.catalogModel !== undefined
    }

    function agentRunning() {
        return root.hasController() && root.agentEditController.running
    }

    function agentVisualMode() {
        if (root.agentRunning()) {
            if (root.hasController() && root.agentEditController.awaitingUserInput)
                return "thinking"
            const event = root.activeActivityEvent()
            const stage = (String(event.key || "") + " " + String(event.title || "") + " " +
                           String(event.detail || "")).toLowerCase()
            if (stage.indexOf("inspect") >= 0 || stage.indexOf("analy") >= 0 ||
                    stage.indexOf("plan") >= 0 || stage.indexOf("model") >= 0)
                return "thinking"
            if (stage.indexOf("apply") >= 0 || stage.indexOf("adjust") >= 0)
                return "adjust"
            return "processing"
        }
        if (root.hasRunFailure() || root.wasRunCanceled())
            return "warning"
        if (root.reviewIssues().length > 0)
            return "warning"
        if (root.editCommitted || root.canReviewDraft())
            return "success"
        if (root.currentPage === 1)
            return "point"
        if (root.currentPage === 2)
            return "adjust"
        return "idle"
    }

    function agentVisualTool() {
        if (root.agentRunning()) {
            const event = root.activeActivityEvent()
            const explicitTool = String(event.characterTool || "")
            if (explicitTool.length > 0)
                return explicitTool
            const mode = root.agentVisualMode()
            if (mode === "thinking")
                return "sampler"
            if (mode === "adjust" || mode === "processing")
                return "tone-key"
            return "loupe"
        }
        return "none"
    }

    function activeActivityEvent() {
        const events = root.runEvents()
        if (events.length <= 0)
            return ({})
        for (let index = events.length - 1; index >= 0; --index) {
            const event = events[index] || ({})
            if (String(event.status || "").toLowerCase() === "running")
                return event
        }
        return events[events.length - 1] || ({})
    }

    function agentSpeechTitle() {
        if (root.currentPage === 0)
            return root.scopeCount(root.scopeMode) > 0
                    ? "Is this what you want me to edit?"
                    : "What photo or project should I edit?"
        if (root.currentPage === 1)
            return "What should I change?"
        if (root.currentPage === 2) {
            if (root.agentRunning())
                return root.activityTitle(root.activeActivityEvent())
            if (root.hasRunFailure())
                return root.friendlyFailureMessage()
            if (root.wasRunCanceled())
                return "I stopped safely."
            return "Your draft is ready to review."
        }
        if (root.directionComparisonPending())
            return "Which direction feels more like you?"
        if (root.directionChoiceKey().length > 0)
            return root.directionChoiceLabel() + " is selected."
        if (root.sampleReviewWaiting())
            return "How do these samples feel?"
        if (root.editCommitted)
            return "Done — your edit is in the studio."
        if (root.reviewIssues().length > 0) {
            const count = root.reviewIssues().length
            return count + " result" + (count === 1 ? " needs" : "s need") + " your review."
        }
        return "Does this edit match your direction?"
    }

    function agentSpeechDetail() {
        if (root.currentPage === 0)
            return root.scopeCount(root.scopeMode) > 0
                    ? root.targetSummary() + " is selected. You can choose another photo, a selection, or the whole project."
                    : "Open a photo, select several, or choose the active project."
        if (root.currentPage === 1)
            return "Describe the result in your own words, then give me a look or reference if you want one."
        if (root.currentPage === 2) {
            if (root.agentRunning())
                return root.activityDescription(root.activeActivityEvent()) + "  " + root.elapsedLabel()
            if (root.hasRunFailure() || root.wasRunCanceled())
                return root.friendlyFailureMessage()
            return "I rendered the actual result and checked it against your request."
        }
        if (root.directionComparisonPending())
            return "These are two actual rendered edits. Pick one to keep working with; neither choice becomes memory until you apply it."
        if (root.directionChoiceKey().length > 0)
            return "Your selected render is still reversible. Apply it to approve the edit and teach this project taste."
        if (root.sampleReviewWaiting())
            return "Compare the real samples first. The rest of the batch stays untouched until you approve this direction."
        if (root.editCommitted)
            return "The verified draft was applied to the exact target shown here."
        if (root.reviewIssues().length > 0)
            return root.exceptionReviewHeadline() + ". Only the exceptions are shown; fix, keep, or revert them before anything is applied."
        return "Compare the real before and after. Nothing changes permanently until you approve it."
    }

    function workflowState() {
        if (!root.hasController())
            return ({})
        const checkingModel = Boolean(root.agentEditController.modelProbeRunning)
        const state = root.agentEditController.aiEditWorkflowState(root.scopeMode) || ({})
        state.modelProbeRunning = checkingModel
        return state
    }

    function sampleReviewWaiting() {
        return Boolean(root.workflowState().sampleReviewWaiting)
    }

    function sampleReview() {
        return root.workflowState().sampleReview || ({})
    }

    function correctionTransferPreview() {
        return root.lastReport().correctionTransferPreview || ({})
    }

    function correctionTransferAvailable() {
        return Boolean(root.correctionTransferPreview().available) &&
                String(root.correctionTransferPreview().state || "") ===
                "waiting-for-approval"
    }

    function correctionTransferHeadline() {
        const preview = root.correctionTransferPreview()
        const provided = String(preview.headline || "").trim()
        if (provided.length > 0)
            return provided
        const remaining = Number(preview.remainingCount || 0)
        return "Your correction is ready for " + remaining + " more photo" +
                (remaining === 1 ? "" : "s")
    }

    function correctionTransferLesson() {
        const feedback = String(root.correctionTransferPreview().feedback || "").trim()
        return feedback.length > 0 ? "“" + feedback + "”" : "Your sample correction"
    }

    function correctionTransferDetail() {
        const preview = root.correctionTransferPreview()
        const provided = String(preview.explanation || "").trim()
        if (provided.length > 0)
            return provided
        return "The rest stay untouched until you approve Apply to All."
    }

    function directionComparison() {
        if (root.hasController() && root.agentEditController.draftDirectionComparison !== undefined)
            return root.agentEditController.draftDirectionComparison || ({})
        return root.workflowState().directionComparison || ({})
    }

    function directionCards() {
        return root.directionComparison().directions || []
    }

    function directionCard(key) {
        const cards = root.directionCards()
        for (let index = 0; index < cards.length; ++index) {
            const card = cards[index] || ({})
            if (String(card.key || "") === key)
                return card
        }
        return ({})
    }

    function hasDirectionComparison() {
        return root.directionCards().length >= 2
    }

    function directionComparisonPending() {
        return Boolean(root.directionComparison().choiceRequired) && root.hasDirectionComparison()
    }

    function directionChoiceKey() {
        return String(root.directionComparison().chosenKey || "")
    }

    function directionChoiceLabel() {
        const card = root.directionCard(root.directionChoiceKey())
        return String(card.label || "Direction")
    }

    function directionComparisonFromCalibration() {
        return String(root.directionComparison().comparisonOrigin || "") ===
                "photographer-calibration"
    }

    function directionDisplayLabel(key) {
        if (!root.directionComparisonFromCalibration())
            return key === "a" ? "DIRECTION A" : "DIRECTION B"
        return key === "a" ? "CURRENT DIRECTION" : "MORE RESTRAINED"
    }

    function photographerTasteCheck() {
        if (!root.hasController() ||
                typeof root.agentEditController.photographerTasteCheck !== "function")
            return root.workflowState().photographerTasteCheck || ({})
        const calibration = root.agentEditController.photographerCalibrationProfile || ({})
        if (!Boolean(calibration.available))
            return ({})
        return root.agentEditController.photographerTasteCheck() || ({})
    }

    function photographerTasteCheckRecommended() {
        return Boolean(root.photographerTasteCheck().recommended) &&
                !root.hasDirectionComparison()
    }

    function directionProofSource(key) {
        const path = String(root.directionCard(key).afterProofPath || "")
        return path.length > 0 ? root.normalizeImageSource(path) : ""
    }

    function sampleCount() {
        return Number(root.sampleReview().sampleAssetIds ? root.sampleReview().sampleAssetIds.length : 0)
    }

    function reviewSamples() {
        return root.sampleReview().samples || []
    }

    function displayedSampleIndex() {
        const samples = root.reviewSamples()
        if (samples.length <= 0)
            return 0
        return Math.max(0, Math.min(root.activeReviewSampleIndex,
                                    samples.length - 1))
    }

    function displayedSample() {
        const samples = root.reviewSamples()
        return samples.length > 0 ? (samples[root.displayedSampleIndex()] || ({})) : ({})
    }

    function canShowPreviousSample() {
        return root.sampleReviewWaiting() && root.displayedSampleIndex() > 0
    }

    function canShowNextSample() {
        return root.sampleReviewWaiting() &&
                root.displayedSampleIndex() + 1 < root.reviewSamples().length
    }

    function showPreviousSample() {
        if (root.canShowPreviousSample())
            root.activeReviewSampleIndex = root.displayedSampleIndex() - 1
    }

    function showNextSample() {
        if (root.canShowNextSample())
            root.activeReviewSampleIndex = root.displayedSampleIndex() + 1
    }

    function displayedSamplePositionText() {
        return root.reviewSamples().length > 0
                ? (root.displayedSampleIndex() + 1) + " / " + root.reviewSamples().length
                : "0 / 0"
    }

    function sampleRemainingCount() {
        return Number(root.workflowState().sampleRemainingCount || 0)
    }

    function assignmentStoryMap() {
        const report = root.lastReport()
        const draft = report.draftReport || ({})
        return report.assignmentStoryMap || draft.assignmentStoryMap ||
                root.workflowState().assignmentStoryMap || ({})
    }

    function assignmentStoryMapAvailable() {
        return Boolean(root.assignmentStoryMap().available) &&
                !root.hasDirectionComparison()
    }

    function approvedAssignmentStoryReceipt() {
        const report = root.lastReport()
        const summary = root.batchSummary()
        return report.approvedAssignmentStoryReceipt ||
                summary.approvedAssignmentStoryReceipt || ({})
    }

    function approvedStoryDirectionAvailable() {
        const receipt = root.approvedAssignmentStoryReceipt()
        return Boolean(receipt.available) && Boolean(receipt.approved)
    }

    function assignmentStoryEyebrow() {
        return root.approvedStoryDirectionAvailable()
                ? "STORY DIRECTION · APPROVED FOR DELIVERY"
                : "STORY MAP · REVIEW COVERAGE"
    }

    function assignmentStoryHeadline() {
        const receipt = root.approvedAssignmentStoryReceipt()
        return String((root.approvedStoryDirectionAvailable() ? receipt.headline : "") ||
                      root.assignmentStoryMap().headline ||
                      "Review the visual story before the full set")
    }

    function assignmentStorySummary() {
        const receipt = root.approvedAssignmentStoryReceipt()
        return String((root.approvedStoryDirectionAvailable() ? receipt.summary : "") ||
                      root.assignmentStoryMap().summary ||
                      "Story anchors join the lighting representatives in this review.")
    }

    function assignmentStoryReceiptText() {
        if (root.storyPreferenceRemembered())
            return "REMEMBERED FOR THIS PROJECT · FUTURE STORY SUGGESTIONS ONLY · NO RECIPE OR CULLING INFLUENCE"
        if (root.approvedStoryDirectionAvailable())
            return "APPROVED ARC · SOURCE ORDER KEPT · NO AUTO-CULL · NO ROLE-BASED RECIPE CHANGES"
        return root.assignmentStoryMap().photographerSteered
                ? "PHOTOGRAPHER STEERED · REAL PROOFS REBUILT · SOURCE ORDER KEPT"
                : "CURATION + SEQUENCE + LIGHTING · NO AUTO-CULL · SOURCE ORDER KEPT"
    }

    function assignmentStoryArcText() {
        const cards = root.assignmentStoryMap().roleCards || []
        const labels = []
        for (let index = 0; index < cards.length; ++index) {
            const label = String((cards[index] || ({})).label || "")
            if (label.length > 0)
                labels.push(label)
        }
        return labels.join("  →  ")
    }

    function shownStoryAssetId() {
        return String(root.displayedSample().assetId || "")
    }

    function shownStoryAssignment() {
        const assignments = root.assignmentStoryMap().assignmentsByAssetId || ({})
        return assignments[root.shownStoryAssetId()] || ({})
    }

    function shownStoryRole() {
        return String(root.shownStoryAssignment().role || "")
    }

    function shownStoryRoleEvidence() {
        return root.shownStoryAssignment().roleEvidence || ({})
    }

    function shownStoryRoleEvidenceLabel() {
        const evidence = root.shownStoryRoleEvidence()
        if (Boolean(evidence.photographerAssigned))
            return "YOUR DIRECTION"
        const basis = String(evidence.basis || "")
        if (basis === "explicit-curation")
            return "YOUR CURATION"
        if (basis === "lighting-group-boundary")
            return "LIGHTING + SEQUENCE"
        if (basis === "source-order" || basis === "sequence-edge")
            return "SOURCE ORDER"
        if (basis === "capture-group-variation")
            return "CAPTURE GROUP"
        if (basis === "dominant-lighting-or-sequence")
            return "LIGHTING + SEQUENCE"
        if (basis === "remembered-photographer-story-preference")
            return "REMEMBERED DIRECTION"
        return "LIGHTING GROUP"
    }

    function storyPreferenceStatus() {
        if (!root.hasController() ||
                typeof root.agentEditController.storyPreferenceStatus !== "function")
            return ({ available: false })
        return root.agentEditController.storyPreferenceStatus() || ({ available: false })
    }

    function storyPreferenceRemembered() {
        return Boolean(root.storyPreferenceStatus().available)
    }

    function canManageStoryPreference() {
        return root.editCommitted && root.approvedStoryDirectionAvailable() &&
                root.hasController() &&
                typeof root.agentEditController.rememberApprovedStoryPreference === "function" &&
                typeof root.agentEditController.forgetApprovedStoryPreference === "function"
    }

    function storyPreferenceActionText() {
        if (!root.storyPreferenceRemembered())
            return "Remember this story"
        return root.storyPreferenceForgetArmed
                ? "Confirm forget"
                : "Forget remembered story"
    }

    function manageStoryPreference() {
        if (!root.canManageStoryPreference())
            return ({ ok: false, skipped: true })
        if (root.storyPreferenceRemembered() &&
                !root.storyPreferenceForgetArmed) {
            root.storyPreferenceForgetArmed = true
            root.storyPreferenceActionMessage =
                    "Existing edits stay unchanged. Click again to forget it for future story suggestions."
            return ({ ok: false, confirmationRequired: true })
        }
        const forgetting = root.storyPreferenceRemembered()
        const result = forgetting
                ? root.agentEditController.forgetApprovedStoryPreference(true)
                : root.agentEditController.rememberApprovedStoryPreference(true)
        if (Boolean((result || ({})).ok)) {
            root.storyPreferenceForgetArmed = false
            root.storyPreferenceActionMessage = forgetting
                    ? "Forgotten for future story suggestions. Existing edits did not change."
                    : "Remembered for this project. Future sets still require fresh sample review."
            root.refreshDetailsSnapshot()
        } else {
            const errors = (result || ({})).errors || []
            root.storyPreferenceActionMessage = errors.length > 0
                    ? errors.join("\n") : "The story preference could not be updated."
        }
        return result || ({})
    }

    function shownStoryRoleReason() {
        return String(root.shownStoryAssignment().reason ||
                      "This frame is included to cover the assignment's validated edit groups.")
    }

    function shownStoryRoleEvidenceTitle() {
        return "WHY " + root.assignmentStoryRoleLabel(
                    root.shownStoryRole()).toUpperCase() + " · " +
                root.shownStoryRoleEvidenceLabel()
    }

    function assignmentStoryRoleOptions() {
        return [
            { role: "atmosphere", label: "Atmosphere", detail: "Establish the place and feeling" },
            { role: "hero", label: "Hero", detail: "Carry the assignment's strongest moment" },
            { role: "detail", label: "Detail", detail: "Hold texture, gesture, or evidence" },
            { role: "transition", label: "Transition", detail: "Bridge scenes, light, or pacing" },
            { role: "support", label: "Support", detail: "Strengthen the set without leading it" }
        ]
    }

    function assignmentStoryRoleLabel(role) {
        const options = root.assignmentStoryRoleOptions()
        for (let index = 0; index < options.length; ++index) {
            if (String(options[index].role) === String(role))
                return String(options[index].label)
        }
        return "Unassigned"
    }

    function canDirectShownStoryRole() {
        return root.assignmentStoryMapAvailable() && root.sampleReviewWaiting() &&
                root.shownStoryAssetId().length > 0 && root.hasController() &&
                typeof root.agentEditController.reviseAssignmentStoryRole === "function"
    }

    function canAssignShownStoryRole(role) {
        return root.canDirectShownStoryRole() &&
                root.shownStoryRole() !== String(role || "")
    }

    function canPromoteShownStoryHero() {
        return root.canAssignShownStoryRole("hero")
    }

    function assignShownStoryRole(role) {
        const requestedRole = String(role || "")
        if (!root.canAssignShownStoryRole(requestedRole))
            return ({ ok: false, skipped: true, role: requestedRole })
        root.applyError = ""
        root.setPage(2)
        const result = root.agentEditController.reviseAssignmentStoryRole(
                         root.shownStoryAssetId(), requestedRole) || ({})
        if (!Boolean(result.ok)) {
            const errors = result.errors || []
            root.applyError = errors.length > 0 ? errors.join("\n") :
                                                  "The story samples could not be rebuilt."
        }
        if (root.canReviewDraft())
            root.setPage(3)
        return result
    }

    function promoteShownStoryHero() {
        return root.assignShownStoryRole("hero")
    }

    function batchSummary() {
        const report = root.lastReport()
        return report.batchSummary || (report.draftReport ? report.draftReport.batchSummary || ({}) : ({}))
    }

    function reviewIssues() {
        return root.sampleReviewWaiting() ? [] : (root.batchSummary().reviewIssues || [])
    }

    function reviewSummary() {
        return root.batchSummary().reviewSummary || ({})
    }

    function consistencyMap() {
        return root.batchSummary().consistencyMap || ({})
    }

    function consistencyGroups() {
        return root.consistencyMap().groups || []
    }

    function consistencyHeadline() {
        return String(root.consistencyMap().headline || "Consistency proof is building")
    }

    function consistencySummary() {
        return String(root.consistencyMap().summary ||
                      "Tone, color, and skin response are checked inside each lighting group.")
    }

    function exceptionReviewHeadline() {
        const summary = root.reviewSummary()
        const headline = String(summary.headline || "")
        if (headline.length > 0)
            return headline
        const verified = Number(summary.verifiedCount || summary.passedCount || 0)
        const issues = root.reviewIssues().length
        return verified + " verified · " + issues + " need" + (issues === 1 ? "s" : "") + " review"
    }

    function exceptionReviewFootnote() {
        const verified = Number(root.reviewSummary().hiddenVerifiedCount ||
                                root.reviewSummary().verifiedCount || 0)
        return verified + " verified photo" + (verified === 1 ? " is" : "s are") +
                " collapsed into this receipt. Your review workload is only the " +
                root.reviewIssues().length + " exception" +
                (root.reviewIssues().length === 1 ? "." : "s.")
    }

    function affectedPhotoCount() {
        const state = root.workflowState()
        return Number(state.assetCount || root.targetAssetIds().length)
    }

    function affectedPhotoSummary() {
        const count = root.affectedPhotoCount()
        if (root.sampleReviewWaiting())
            return root.sampleCount() + " sample" + (root.sampleCount() === 1 ? "" : "s") +
                    " shown · " + root.sampleRemainingCount() + " more wait for approval"
        return count + " affected photo" + (count === 1 ? "" : "s")
    }

    function riskAssessment() {
        const report = root.lastReport()
        const draft = report.draftReport || ({})
        return report.riskAssessment || draft.riskAssessment || ({})
    }

    function approvalRequests() {
        return root.riskAssessment().approvalRequests || []
    }

    function sensitiveApprovalRequired() {
        return Boolean(root.riskAssessment().approvalRequired) && root.approvalRequests().length > 0
    }

    function sensitiveApprovalSummary() {
        const requests = root.approvalRequests()
        const labels = []
        for (let index = 0; index < requests.length; ++index) {
            const request = requests[index] || ({})
            const label = String(request.label || request.type || "Sensitive change")
            if (labels.indexOf(label) < 0)
                labels.push(label)
        }
        return labels.join(" · ")
    }

    function pendingApprovalQuestion() {
        if (!root.hasController() || !root.agentEditController.awaitingUserInput)
            return ""
        const questions = root.agentEditController.pendingUserQuestions || []
        if (questions.length <= 0)
            return "This edit includes a sensitive change. Confirm before a draft is created."
        const question = questions[0] || ({})
        return String(question.question || question.prompt || question.summary || question)
    }

    function setPage(page) {
        root.currentPage = Math.max(0, Math.min(3, page))
    }

    function nextPage() {
        root.setPage(root.currentPage + 1)
    }

    function previousPage() {
        root.setPage(root.currentPage - 1)
    }

    function suggestionKey(suggestion) {
        const item = suggestion || ({})
        return String(item.kind || "item") + ":" + String(item.id || "")
    }

    function suggestionSelected(suggestion) {
        return root.selectedSuggestionKeys.indexOf(root.suggestionKey(suggestion)) >= 0
    }

    function toggleSuggestion(suggestion) {
        const item = suggestion || ({})
        const key = root.suggestionKey(item)
        if (String(item.id || "").length === 0)
            return
        let next = root.selectedSuggestionKeys.slice(0)
        if (String(item.kind || "") === "preset") {
            next = next.filter(function(existing) { return String(existing).indexOf("preset:") !== 0 })
        }
        const existing = next.indexOf(key)
        if (existing >= 0)
            next.splice(existing, 1)
        else
            next.push(key)
        root.selectedSuggestionKeys = next
        if (String(item.kind || "") === "photo") {
            root.inspirationAssetIds = root.selectedInspirationPhotoIds()
            root.preparedInspirationSignature = ""
        }
        if (String(item.kind || "") === "preset" && existing < 0 && root.hasController())
            root.agentEditController.selectedStyleId = String(item.id)
    }

    function selectedInspirationPhotoIds() {
        const ids = []
        for (let index = 0; index < root.inspirationSuggestions.length; ++index) {
            const suggestion = root.inspirationSuggestions[index] || ({})
            if (String(suggestion.kind || "") === "photo" && root.suggestionSelected(suggestion))
                ids.push(String(suggestion.id || ""))
        }
        return ids
    }

    function selectedInspirationStyleId() {
        for (let index = 0; index < root.inspirationSuggestions.length; ++index) {
            const suggestion = root.inspirationSuggestions[index] || ({})
            if (String(suggestion.kind || "") === "preset" && root.suggestionSelected(suggestion))
                return String(suggestion.id || "")
        }
        return root.selectedStyleId()
    }

    function styleReferencePreflight() {
        if (!root.hasController() ||
                typeof root.agentEditController.styleReferencePreflight !== "function")
            return ({})
        return root.agentEditController.styleReferencePreflight(
                    root.selectedInspirationPhotoIds()) || ({})
    }

    function styleReferenceStatusText() {
        const count = root.selectedInspirationPhotoIds().length
        if (count <= 0)
            return "Choose at least one edited photo to teach this project look"
        const preflight = root.styleReferencePreflight()
        if (Boolean(preflight.warningRequired) || !Boolean(preflight.available))
            return String(preflight.reason || "Check the selected style references.")
        const paired = Number(preflight.pairedOriginalCount || 0)
        return count + " edited reference photo" + (count === 1 ? "" : "s") +
                (paired > 0 ? " · original pairing found automatically" : " · ready to learn")
    }

    function preparePromptSuggestions() {
        if (!root.hasController())
            return
        const prompt = String(root.agentEditController.userInstructions || "").trim()
        if (prompt.length === 0) {
            root.directionError = "Describe the edit you want first."
            return
        }
        const report = root.agentEditController.suggestInspirationForPrompt(prompt) || ({})
        if (!Boolean(report.ok)) {
            const errors = report.errors || []
            root.directionError = errors.length > 0 ? String(errors[0]) : "I couldn't prepare inspiration suggestions."
            return
        }
        root.directionError = ""
        root.inspirationSuggestions = (report.styles || []).concat(report.photos || [])
        root.suggestionSummary = "Suggested from your words · " + String(report.modelName || "local model")
        root.selectedSuggestionKeys = []
        if ((report.styles || []).length > 0) {
            const firstStyle = (report.styles || [])[0] || ({})
            root.selectedSuggestionKeys = [root.suggestionKey(firstStyle)]
            if (String(firstStyle.id || "").length > 0)
                root.agentEditController.selectedStyleId = String(firstStyle.id)
        }
        root.setPage(1)
    }

    function beginTargetSelection() {
        if (!root.hasController())
            return
        const prompt = String(root.agentEditController.userInstructions || "").trim()
        if (prompt.length === 0) {
            root.setPage(0)
            root.directionError = "Describe the edit you want first."
            return
        }
        const styleId = root.selectedInspirationStyleId()
        if (styleId.length > 0)
            root.agentEditController.selectedStyleId = styleId
        root.inspirationAssetIds = root.selectedInspirationPhotoIds()
        root.targetSelectionRequested(prompt, root.inspirationAssetIds, styleId)
        root.close()
    }

    function selectedStyleId() {
        return root.hasController() ? String(root.agentEditController.selectedStyleId || "") : ""
    }

    function selectedStyleName() {
        const styles = root.availableStyles()
        const selected = root.selectedStyleId()
        for (let index = 0; index < styles.length; ++index) {
            const style = styles[index] || ({})
            if (String(style.id || "") === selected)
                return String(style.name || style.id || "Inspiration")
        }
        return styles.length > 0 ? String(styles[0].name || "Inspiration") : "No inspiration"
    }

    function selectedStyleData() {
        const styles = root.availableStyles()
        const selected = root.selectedStyleId()
        for (let index = 0; index < styles.length; ++index) {
            const style = styles[index] || ({})
            if (String(style.id || "") === selected)
                return style
        }
        return ({})
    }

    function currentLookDetail() {
        if (!root.hasController())
            return "Choose a look before editing."
        const taste = root.agentEditController.approvedProjectTaste || ({})
        if (Boolean(taste.available))
            return "Restored for this project · approvals and blind ratings only"
        const style = root.selectedStyleData()
        if (String(style.source || "") === "built-in")
            return "Safe built-in starting point · remembered for this project"
        return "Saved project look · advanced settings stay in the background"
    }

    function editingFreedomMode() {
        if (!root.hasController())
            return "balanced"
        return String(root.agentEditController.editingFreedom || "balanced")
    }

    function editingFreedomSummary() {
        const mode = root.editingFreedomMode()
        if (mode === "protect")
            return "Subtle color and tone only. Framing and local areas stay untouched."
        if (mode === "transform")
            return "Bolder color, crop, and local work. Sensitive edits still ask."
        return "Use the project look and local adjustments when they clearly help."
    }

    function setEditingFreedomMode(mode) {
        if (!root.hasController() || root.agentRunning() || root.agentEditController.draftActive)
            return false
        root.agentEditController.editingFreedom = String(mode || "balanced")
        return root.editingFreedomMode() === String(mode || "balanced")
    }

    function builtInLookSuggestions() {
        const suggestions = []
        const styles = root.availableStyles()
        for (let index = 0; index < styles.length; ++index) {
            const style = styles[index] || ({})
            if (String(style.source || "") !== "built-in")
                continue
            suggestions.push({
                kind: "preset",
                id: String(style.id || ""),
                title: String(style.name || style.id || "Look"),
                detail: String(style.goal || ""),
                reason: "Built into Looksy as a safe starting point."
            })
        }
        return suggestions
    }

    function editedLookSuggestions() {
        const suggestions = []
        const assets = root.styleGalleryAssets()
        for (let index = 0; index < assets.length && suggestions.length < 12; ++index) {
            const asset = assets[index] || ({})
            const sourceAssetId = String(asset.sourceAssetId || "")
            const assetId = String(asset.assetId || "")
            const variantName = String(asset.variantName || "").trim()
            const edited = Boolean(asset.hasEdits) || Boolean(asset.edited) ||
                           Boolean(asset.virtualCopy) ||
                           (sourceAssetId.length > 0 && sourceAssetId !== assetId) ||
                           (variantName.length > 0 && variantName.toLowerCase() !== "original")
            if (!edited)
                continue
            suggestions.push({
                kind: "photo",
                id: String(asset.assetId || ""),
                title: root.assetDisplayTitle(asset),
                detail: "Edited library example",
                thumbnailSource: String(asset.thumbnailSource || ""),
                reason: "Learn from its current edit recipe and rendered look."
            })
        }
        return suggestions
    }

    function showBuiltInLooks() {
        const suggestions = root.builtInLookSuggestions()
        if (suggestions.length <= 0) {
            root.directionError = "No built-in looks are available."
            return
        }
        root.styleStartMode = "built-in"
        root.inspirationMode = "preset"
        root.inspirationSuggestions = suggestions
        root.suggestionSummary = "Built-in looks · safe starting points"
        const selectedKey = "preset:" + root.selectedStyleId()
        root.selectedSuggestionKeys = suggestions.some(function(item) {
            return root.suggestionKey(item) === selectedKey
        }) ? [selectedKey] : []
        root.directionError = ""
        root.setPage(1)
    }

    function showEditedLooks() {
        const suggestions = root.editedLookSuggestions()
        if (suggestions.length <= 0) {
            root.directionError = "No edited reference photos are available outside this edit target yet."
            return
        }
        root.styleStartMode = "learn"
        root.inspirationMode = "references"
        root.inspirationSuggestions = suggestions
        root.selectedSuggestionKeys = []
        root.suggestionSummary = "Edited photos · nothing is learned until you choose examples"
        root.directionError = ""
        root.setPage(1)
    }

    function saveWordsAsLook() {
        if (!root.hasController() ||
                typeof root.agentEditController.createStyleFromDescription !== "function")
            return
        const description = String(root.agentEditController.userInstructions || "").trim()
        if (description.length < 4) {
            root.directionError = "Describe the look first, then save those words as the project look."
            return
        }
        const report = root.agentEditController.createStyleFromDescription(description, "") || ({})
        if (!Boolean(report.ok)) {
            const errors = report.errors || []
            root.directionError = errors.length > 0 ? String(errors[0]) : "That look could not be saved."
            return
        }
        root.styleStartMode = "described"
        root.inspirationMode = "preset"
        root.styleStartMessage = "Saved “" + String(report.styleName || root.selectedStyleName()) +
                                 "” for this project."
        root.directionError = ""
    }

    function availableStyles() {
        return root.hasController() ? root.agentEditController.availableStyles || [] : []
    }

    function scopeCount(scope) {
        if (scope === "confirmed-assets") return root.confirmedTargetIds.length
        if (!root.hasCatalog())
            return 0
        if (scope === "current-photo")
            return String(root.catalogModel.selectedAssetId || "").length > 0 ? 1 : 0
        if (scope === "filtered-library")
            return root.filteredScopeActive() ? Number(root.catalogModel.assetCount || 0) : 0
        if (scope === "project") {
            const project = String(root.catalogModel.projectFilter || "all")
            if (project !== "all" && root.catalogModel.assetCountForProject !== undefined)
                return root.catalogModel.assetCountForProject(project)
            return Number(root.catalogModel.assetCount || 0)
        }
        return Number(root.catalogModel.selectedAssetCount || 0)
    }

    function filteredScopeActive() {
        if (!root.hasCatalog())
            return false
        const visibleCount = Number(root.catalogModel.assetCount || 0)
        const totalCount = Number(root.catalogModel.totalAssetCount || visibleCount)
        return visibleCount > 0 && totalCount > visibleCount
    }

    function bestScope(preferred) {
        const candidate = String(preferred || "")
        if (candidate === "confirmed-assets" && root.confirmedTargetIds.length > 0) return candidate
        if (root.scopeCount("selected-assets") > 1)
            return "selected-assets"
        if (candidate.length > 0 && root.scopeCount(candidate) > 0)
            return candidate
        if (root.scopeCount("current-photo") > 0)
            return "current-photo"
        if (root.scopeCount("project") > 0)
            return "project"
        return "current-photo"
    }

    function chooseScope(scope) {
        root.scopeMode = scope
        root.scopeModeRequested(scope)
        root.pruneInspirationReferences()
    }

    function availableScopeTiles() {
        return root.targetTiles().filter(function(tile) {
            return Number((tile || ({})).count || 0) > 0
        })
    }

    function availableScopeLabels() {
        return root.availableScopeTiles().map(function(tile) {
            return String((tile || ({})).title || "Photos")
        })
    }

    function scopeChoiceIndex() {
        const choices = root.availableScopeTiles()
        for (let index = 0; index < choices.length; ++index) {
            if (String((choices[index] || ({})).key || "") === root.scopeMode)
                return index
        }
        return 0
    }

    function chooseAvailableScope(index) {
        const choices = root.availableScopeTiles()
        const choice = choices[Number(index)] || ({})
        const scope = String(choice.key || "")
        if (scope.length > 0)
            root.chooseScope(scope)
    }

    function scopeRiskMessage() {
        const count = root.scopeCount(root.scopeMode)
        if (count <= 0)
            return "No editable photos are available in this scope."

        const assets = root.targetAssets()
        let unavailableCount = 0
        for (let index = 0; index < assets.length; ++index) {
            const asset = assets[index] || ({})
            if (asset.online === false || String(asset.filePath || "").length === 0)
                unavailableCount += 1
        }
        if (unavailableCount > 0) {
            return unavailableCount + " source file" + (unavailableCount === 1 ? " looks" : "s look")
                    + " unavailable. I'll stop before editing a photo whose original cannot be read."
        }
        if (count >= 3) {
            return "I'll edit representative samples first. The rest of the batch stays untouched until you approve the direction."
        }
        return "Change scope here without changing your Library selection."
    }

    function batchReviewForecast() {
        if (!root.hasController() ||
                typeof root.agentEditController.batchReviewForecast !== "function")
            return ({})
        return root.agentEditController.batchReviewForecast(root.scopeMode) || ({})
    }

    function batchForecastLabel() {
        const risk = root.scopeRiskMessage()
        if (risk.indexOf("No editable") === 0 || risk.indexOf("source file") >= 0)
            return risk
        const forecast = root.batchReviewForecast()
        if (!Boolean(forecast.predictionAvailable))
            return risk
        const likelyReady = Math.max(0, Number(forecast.estimatedAutomaticCount || 0))
        const likelyReview = Math.max(0, Number(forecast.estimatedReviewCount || 0))
        if (likelyReady <= 0 && likelyReview <= 0)
            return risk
        return likelyReady + " likely ready · about " + likelyReview +
                " may need your eye · you’ll approve samples first"
    }

    function batchForecastExplanation() {
        const forecast = root.batchReviewForecast()
        const explanation = String(forecast.explanation || "")
        return explanation.length > 0 ? explanation : root.scopeRiskMessage()
    }

    function projectLabel() {
        if (!root.hasCatalog())
            return "Project"
        const project = String(root.catalogModel.projectFilter || "all")
        if (project === "all")
            return "Project Library"
        if (project === "unassigned")
            return "Unassigned Photos"
        return project
    }

    function targetTiles() {
        const currentCount = root.scopeCount("current-photo")
        const selectedCount = root.scopeCount("selected-assets")
        const filteredCount = root.scopeCount("filtered-library")
        const projectCount = root.scopeCount("project")
        return [
            {
                key: "current-photo",
                title: "Current Photo",
                detail: currentCount > 0 ? "Only the photo open in the editor." : "Open a photo first.",
                count: currentCount,
                thumbnailSource: root.hasCatalog() ? String(root.catalogModel.selectedThumbnailSource || "") : "",
                icon: "adjust"
            },
            {
                key: "selected-assets",
                title: "Selected Photos",
                detail: selectedCount > 0 ? "Only the photos selected in the library." : "Select photos in the library first.",
                count: selectedCount,
                thumbnailSource: selectedCount > 0 && root.hasCatalog()
                                 ? String(root.catalogModel.selectedThumbnailSource || "") : "",
                icon: "image"
            },
            {
                key: "filtered-library",
                title: "Filtered Photos",
                detail: filteredCount > 0 ? "Only the photos visible in the current filter." : "Apply a library filter first.",
                count: filteredCount,
                thumbnailSource: filteredCount > 0 && root.hasCatalog()
                                 ? String(root.catalogModel.selectedThumbnailSource || "") : "",
                icon: "filter"
            },
            {
                key: "project",
                title: root.projectLabel(),
                detail: projectCount > 0 ? "Every photo in this project." : "Create or open a project first.",
                count: projectCount,
                thumbnailSource: "",
                icon: "folder"
            }
        ]
    }

    function targetAssets() {
        if (!root.hasCatalog())
            return []
        if (root.scopeMode === "confirmed-assets")
            return root.confirmedTargetIds.map(function(id) { return root.catalogModel.assetById(id) })
        if (root.scopeMode === "current-photo") {
            const current = root.catalogModel.assetAt(Number(root.catalogModel.selectedIndex || 0)) || ({})
            return String(current.assetId || "").length > 0 ? [current] : []
        }
        if (root.scopeMode === "project") {
            const project = String(root.catalogModel.projectFilter || "all")
            return root.catalogModel.assetsForProject(project) || []
        }
        if (root.scopeMode === "filtered-library") {
            const filtered = []
            for (let index = 0; index < Number(root.catalogModel.assetCount || 0); ++index) {
                const asset = root.catalogModel.assetAt(index) || ({})
                if (String(asset.assetId || "").length > 0)
                    filtered.push(asset)
            }
            return filtered
        }
        const selectedIds = root.catalogModel.selectedAssetIds || []
        const selected = []
        for (let index = 0; index < Number(root.catalogModel.assetCount || 0); ++index) {
            const asset = root.catalogModel.assetAt(index) || ({})
            if (selectedIds.indexOf(String(asset.assetId || "")) >= 0)
                selected.push(asset)
        }
        return selected
    }

    function targetSummary() {
        const count = root.scopeCount(root.scopeMode)
        if (root.scopeMode === "confirmed-assets") return count + " confirmed photo" + (count === 1 ? "" : "s")
        if (root.scopeMode === "current-photo") {
            const assets = root.targetAssets()
            return assets.length > 0 ? root.assetDisplayTitle(assets[0]) : "No current photo"
        }
        if (root.scopeMode === "project")
            return root.projectLabel() + " · " + count + " photo" + (count === 1 ? "" : "s")
        if (root.scopeMode === "filtered-library")
            return count + " filtered photo" + (count === 1 ? "" : "s")
        return count + " selected photo" + (count === 1 ? "" : "s")
    }

    function targetAssetIds() {
        const ids = []
        const assets = root.targetAssets()
        for (let index = 0; index < assets.length; ++index) {
            const assetId = String((assets[index] || ({})).assetId || "")
            if (assetId.length > 0)
                ids.push(assetId)
        }
        return ids
    }

    function isTargetAsset(asset) {
        const assetId = String((asset || ({})).assetId || "")
        return assetId.length > 0 && root.targetAssetIds().indexOf(assetId) >= 0
    }

    function pruneInspirationReferences() {
        const targetIds = root.targetAssetIds()
        root.inspirationAssetIds = root.inspirationAssetIds.filter(function(assetId) {
            return targetIds.indexOf(String(assetId || "")) < 0
        })
        root.preparedInspirationSignature = ""
    }

    function inspirationReferenceCount() {
        return root.inspirationAssetIds.length
    }

    function styleLearningAvailable() {
        const state = root.workflowState()
        const learning = state.styleLearning || ({})
        return Boolean(learning.available)
    }

    function selectedReferenceLabel() {
        const count = root.inspirationReferenceCount()
        if (count <= 0)
            return "No reference photos selected"
        return count + " inspiration reference" + (count === 1 ? "" : "s") + " · these will not be edited"
    }

    function inspirationSummary() {
        if (root.inspirationMode === "references")
            return root.inspirationReferenceCount() + " reference photo" +
                    (root.inspirationReferenceCount() === 1 ? "" : "s")
        return root.selectedStyleName()
    }

    function assetSelected(asset) {
        const assetId = String((asset || ({})).assetId || "")
        return assetId.length > 0 && root.inspirationAssetIds.indexOf(assetId) >= 0
    }

    function assetDisplayTitle(asset) {
        const item = asset || ({})
        const label = String(item.filePath || item.assetId || item.cameraLabel || "Photo")
        const slash = Math.max(label.lastIndexOf("/"), label.lastIndexOf("\\"))
        return slash >= 0 ? label.substring(slash + 1) : label
    }

    function assetMatchesStyleFilter(asset) {
        const item = asset || ({})
        if (root.styleGalleryFilter === "selected" && !root.assetSelected(item))
            return false
        if (root.styleGalleryFilter === "picked" && !Boolean(item.picked))
            return false
        if (root.styleGalleryFilter === "rated" && Number(item.rating || 0) <= 0)
            return false
        if (root.styleGalleryFilter === "project" &&
                String(item.projectSummary || "").length === 0 &&
                String(item.projectNames || "").length === 0) {
            return false
        }

        const query = root.styleGalleryQuery.trim().toLowerCase()
        if (query.length === 0)
            return true
        const haystack = [
            root.assetDisplayTitle(item),
            String(item.captureDate || ""),
            String(item.lensModel || ""),
            String(item.projectSummary || ""),
            String(item.filePath || "")
        ].join(" ").toLowerCase()
        return haystack.indexOf(query) >= 0
    }

    function styleGalleryAssets() {
        const assets = []
        if (!root.hasCatalog())
            return assets
        const count = Math.min(Number(root.catalogModel.assetCount || 0), 96)
        for (let index = 0; index < count; ++index) {
            const asset = root.catalogModel.assetAt(index) || ({})
            if (String(asset.assetId || "").length === 0 || root.isTargetAsset(asset) ||
                    !root.assetMatchesStyleFilter(asset))
                continue
            asset.visibleIndex = index
            assets.push(asset)
        }
        return assets
    }

    function chooseStyleGalleryAsset(asset) {
        const assetId = String((asset || ({})).assetId || "")
        if (assetId.length === 0 || root.isTargetAsset(asset))
            return
        const nextIds = root.inspirationAssetIds.slice(0)
        const existingIndex = nextIds.indexOf(assetId)
        if (existingIndex >= 0)
            nextIds.splice(existingIndex, 1)
        else
            nextIds.push(assetId)
        root.inspirationAssetIds = nextIds
        root.inspirationMode = "references"
        root.preparedInspirationSignature = ""
        root.directionError = ""
    }

    function saveSelectedReferencesAsStyle() {
        root.learnSelectedInspiration()
    }

    function normalizeImageSource(source) {
        const text = String(source || "")
        if (text.length === 0)
            return ""
        if (text.indexOf("file:") === 0 ||
                text.indexOf("qrc:") === 0 ||
                text.indexOf("image:") === 0 ||
                text.indexOf("http:") === 0 ||
                text.indexOf("https:") === 0) {
            return text
        }
        if (text.charAt(0) === "/")
            return "file://" + text
        return text
    }

    function styleThumbnailSource(style) {
        const data = style || ({})
        return root.normalizeImageSource(data.thumbnailSource ||
                                         data.thumbnail ||
                                         data.previewSource ||
                                         data.preview ||
                                         data.imageSource ||
                                         "")
    }

    function styleAccent(style, slot) {
        const id = String((style || ({})).id || "")
        if (id.indexOf("portrait") >= 0)
            return slot === 0 ? "#F06A5D" : "#282B31"
        if (id.indexOf("wedding") >= 0 || id.indexOf("warm") >= 0)
            return slot === 0 ? "#EEE9DF" : "#F06A5D"
        if (id.indexOf("product") >= 0 || id.indexOf("commercial") >= 0)
            return slot === 0 ? "#F4F0E8" : "#31343A"
        if (id.indexOf("landscape") >= 0)
            return slot === 0 ? "#3A3D43" : "#E8E3DA"
        if (id.indexOf("black") >= 0 || id.indexOf("white") >= 0)
            return slot === 0 ? "#F1EEE7" : "#24272C"
        if (id.indexOf("street") >= 0)
            return slot === 0 ? "#F06A5D" : "#17191D"
        return slot === 0 ? "#34373D" : "#F06A5D"
    }

    function inspirationTiles() {
        const tiles = []
        tiles.push({
            kind: "learn",
            styleId: "",
            title: "Selected Photos",
            detail: root.inspirationReferenceCount() > 0
                    ? "Learn the look from your edited references."
                    : "Select edited reference photos in the library.",
            thumbnailSource: root.hasCatalog() ? root.normalizeImageSource(root.catalogModel.selectedThumbnailSource || "") : "",
            enabled: root.styleLearningAvailable(),
            accentA: "#D8DCE6",
            accentB: "#A87B5B"
        })

        const styles = root.availableStyles()
        for (let index = 0; index < styles.length; ++index) {
            const style = styles[index] || ({})
            tiles.push({
                kind: "style",
                styleId: String(style.id || ""),
                title: String(style.name || style.id || "Style"),
                detail: String(style.goal || style.description || style.summary || ""),
                thumbnailSource: root.styleThumbnailSource(style),
                enabled: String(style.id || "").length > 0,
                accentA: root.styleAccent(style, 0),
                accentB: root.styleAccent(style, 1)
            })
        }
        return tiles
    }

    function chooseInspiration(tile) {
        const item = tile || ({})
        if (!Boolean(item.enabled))
            return
        if (String(item.kind || "") === "learn") {
            root.inspirationMode = "references"
            root.learnSelectedInspiration()
            return
        }
        if (root.hasController() && String(item.styleId || "").length > 0) {
            root.inspirationMode = "preset"
            root.agentEditController.selectedStyleId = String(item.styleId)
        }
    }

    function learnSelectedInspiration() {
        if (!root.hasController())
            return false
        const count = root.inspirationReferenceCount()
        if (count <= 0) {
            root.directionError = "Choose at least one edited reference photo, or use a preset look."
            return false
        }
        const instructions = String(root.agentEditController.userInstructions || "").trim()
        const label = count > 1 ? "Reference Look" : "Reference Photo Look"
        const result = root.agentEditController.createLearnedStyleProfileFromAssets(
                    label,
                    instructions.length > 0
                        ? "Match the selected references while following this edit goal: " + instructions
                        : "Match the selected reference look without changing the subject or composition.",
                    instructions,
                    root.inspirationAssetIds)
        if (!Boolean((result || ({})).ok)) {
            const errors = (result || ({})).errors || []
            root.directionError = errors.length > 0 ? String(errors[0]) : "The reference look could not be prepared."
            return false
        }
        root.preparedInspirationSignature = root.inspirationAssetIds.join("|") + "|" + instructions
        const excluded = Number((result || ({})).excludedUneditedReferenceCount || 0)
        root.styleStartMessage = excluded > 0
                ? "Learned the look and ignored " + excluded + " unrelated original" +
                  (excluded === 1 ? "." : "s.")
                : "Learned this look from the selected edited photos."
        root.directionError = ""
        return true
    }

    function runAgent() {
        if (!root.hasController())
            return
        const instructions = String(root.agentEditController.userInstructions || "").trim()
        if (instructions.length === 0) {
            root.directionError = "Describe the result you want before creating a draft."
            root.setPage(1)
            return
        }
        if (root.inspirationMode === "references") {
            const signature = root.inspirationAssetIds.join("|") + "|" + instructions
            if (signature !== root.preparedInspirationSignature && !root.learnSelectedInspiration()) {
                root.setPage(1)
                return
            }
        }
        root.directionError = ""
        root.setPage(2)
        root.runRequested()
    }

    function presentAppliedResult(report) {
        const result = report || ({})
        const commit = result.commitReport || result
        root.applyError = ""
        root.committedAssetIds = commit.assetIds || result.assetIds || []
        root.editCommitted = true
        root.editApplied(root.committedAssetIds)
        root.setPage(3)
    }

    function activeModel() {
        const models = root.hasController() ? root.agentEditController.availableModels || [] : []
        const selected = root.hasController() ? String(root.agentEditController.selectedModelId || "") : ""
        for (let index = 0; index < models.length; ++index) {
            if (String((models[index] || ({})).id || "") === selected)
                return models[index] || ({})
        }
        return ({})
    }

    function activeModelLabel() {
        const model = root.activeModel()
        const name = String(model.name || model.id || "No model")
        const backend = String(model.backend || "local")
        const report = root.lastReport()
        const draft = report.draftReport || ({})
        const audit = report.modelPromptAudit || draft.modelPromptAudit || ({})
        if (Boolean(audit.fallbackUsed))
            return name + " · safe local fallback"
        return backend === "ollama" ? name + " · Local LLM" : name + " · deterministic fallback"
    }

    function retouchRuntimeLabel() {
        const workflow = root.workflowState()
        const runtime = workflow.imageRuntime || root.lastReport().retouchRuntime || ({})
        if (Boolean(runtime.inpaintAvailable))
            return Boolean(runtime.bundleVerified) && Boolean(runtime.inpaintArtifactVerified)
                    ? "Verified professional cleanup ready"
                    : "Professional cleanup ready"
        const bundleErrorCode = String(runtime.bundleVerificationErrorCode || "")
        if (bundleErrorCode.length > 0 && bundleErrorCode !== "manifest_missing")
            return "Cleanup fallback · model bundle failed verification · failed proofs stay unchanged"
        return "Cleanup fallback · failed proofs stay unchanged"
    }

    function localToolsLabel() {
        if (!root.hasController())
            return "Unavailable"
        const endpoint = String(root.agentEditController.mcpEndpoint || "")
        if (endpoint.length === 0)
            return "Built-in editing tools"
        const status = String(root.agentEditController.mcpStatus || "Ready")
        const toolCount = Number(root.agentEditController.mcpToolCount || 0)
        if (Boolean(root.agentEditController.mcpAvailable))
            return "Ready · " + toolCount + " local tool" + (toolCount === 1 ? "" : "s")
        return status.length > 0 ? status : "Local tools unavailable"
    }

    function tasteMirrorLabel() {
        if (!root.hasController())
            return "Unavailable"
        const taste = root.agentEditController.approvedProjectTaste || ({})
        if (!Boolean(taste.available))
            return "Learns only from approved edits"
        const evidenceCount = Number(taste.evidenceCount || 0)
        return "Project-only · " + evidenceCount + " approved signal" +
                (evidenceCount === 1 ? "" : "s")
    }

    function setDetailsExpanded(expanded) {
        root.detailsExpanded = Boolean(expanded)
        if (root.detailsExpanded)
            root.refreshDetailsSnapshot()
    }

    function refreshDetailsSnapshot() {
        if (!root.hasController() ||
                typeof root.agentEditController.memoryVaultStatus !== "function") {
            root.memoryVaultSnapshot = ({})
            return
        }
        root.memoryVaultSnapshot = root.agentEditController.memoryVaultStatus() || ({})
    }

    function mcpEndpointLabel() {
        if (!root.hasController())
            return "No endpoint"
        const endpoint = String(root.agentEditController.mcpEndpoint || "")
        return endpoint.length > 0 ? endpoint : "Built into Looksy"
    }

    function memoryVaultLabel() {
        const snapshot = root.memoryVaultSnapshot || ({})
        const noteCount = Number(snapshot.totalNoteCount || 0)
        const parts = [noteCount + " local note" + (noteCount === 1 ? "" : "s")]
        if (Boolean(snapshot.externalChangeDetected))
            parts.push(Number(snapshot.externalChangeCount || 0) + " outside change" +
                       (Number(snapshot.externalChangeCount || 0) === 1 ? "" : "s"))
        else
            parts.push("project memory current")
        return parts.join(" · ")
    }

    function latestStyleChangeSummary() {
        const report = root.lastReport()
        const direct = String(report.styleChangeSummary || "")
        if (direct.length > 0)
            return direct
        const events = report.userVisibleMemoryEvents || []
        for (let index = events.length - 1; index >= 0; --index) {
            const event = events[index] || ({})
            const summary = String(event.styleChangeSummary || event.summary || "")
            if (summary.length > 0)
                return summary
        }
        return "No approved taste change in this run"
    }

    function runAuditSummary() {
        const tools = root.toolCalls()
        const warnings = root.runWarnings()
        const parts = [root.validationLabel()]
        parts.push(tools.length + " tool call" + (tools.length === 1 ? "" : "s"))
        if (warnings.length > 0)
            parts.push(warnings.length + " warning" + (warnings.length === 1 ? "" : "s"))
        return parts.join(" · ")
    }

    function latestToolCallSummary() {
        const tools = root.toolCalls()
        if (tools.length <= 0)
            return "No tool calls recorded yet"
        const latest = tools[tools.length - 1] || ({})
        return root.toolCallTitle(latest) + " · " + root.toolCallDetail(latest)
    }

    function proofArtifactLabel() {
        const report = root.lastReport()
        const draft = report.draftReport || ({})
        const path = String(report.shareableManifestPath || report.manifestPath ||
                            report.currentProofPath || draft.shareableManifestPath ||
                            draft.manifestPath || draft.currentProofPath || "")
        if (path.length === 0)
            return "Proof artifact appears after the first rendered draft"
        const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return slash >= 0 ? path.substring(slash + 1) : path
    }

    function detailsActionResult(result, successText) {
        const report = result || ({})
        const errors = report.errors || []
        if (!Boolean(report.ok)) {
            root.detailsActionMessage = errors.length > 0
                    ? String(errors[0]) : "That local memory action could not be completed."
            return
        }
        root.detailsActionMessage = successText
        root.refreshDetailsSnapshot()
    }

    function openMemoryVault() {
        if (root.hasController() && typeof root.agentEditController.openMemoryVault === "function")
            root.detailsActionResult(root.agentEditController.openMemoryVault(), "Opened the local memory folder.")
    }

    function openMemoryVaultInObsidian() {
        if (root.hasController() &&
                typeof root.agentEditController.openMemoryVaultInObsidian === "function") {
            root.detailsActionResult(root.agentEditController.openMemoryVaultInObsidian(false),
                                     "Opened project memory in Obsidian.")
        }
    }

    function exportRedactedMemoryVault() {
        if (root.hasController() &&
                typeof root.agentEditController.exportMemoryVault === "function") {
            root.detailsActionResult(root.agentEditController.exportMemoryVault("", true),
                                     "Exported a redacted copy of agent memory.")
        }
    }

    function runEvents() {
        return root.hasController() ? root.agentEditController.runEvents || [] : []
    }

    function toolCalls() {
        if (!root.hasController())
            return []
        const report = root.agentEditController.lastRunReport || ({})
        const draft = report.draftReport || ({})
        return report.toolCalls || draft.toolCalls || []
    }

    function toolCallTitle(call) {
        const item = call || ({})
        return String(item.name || "tool") + " · " + String(item.status || "recorded")
    }

    function toolCallDetail(call) {
        const item = call || ({})
        const outcome = item.outcome || ({})
        const parts = []
        if (Number(item.durationMs || outcome.durationMs || 0) > 0)
            parts.push(Number(item.durationMs || outcome.durationMs) + " ms")
        if (String(outcome.assetId || "").length > 0)
            parts.push(String(outcome.assetId))
        if (Number(outcome.warningCount || 0) > 0)
            parts.push(Number(outcome.warningCount) + " warning" + (Number(outcome.warningCount) === 1 ? "" : "s"))
        if (String(outcome.error || item.error || "").length > 0)
            parts.push(String(outcome.error || item.error))
        return parts.length > 0 ? parts.join(" · ") : "Completed"
    }

    function runWarnings() {
        if (!root.hasController())
            return []
        const report = root.agentEditController.lastRunReport || ({})
        const draft = report.draftReport || ({})
        const validation = report.validationReport || draft.validationReport || ({})
        const warnings = []
        const add = function(items) {
            for (let index = 0; items && index < items.length; ++index) {
                const value = String(items[index] || "")
                if (value.length > 0 && warnings.indexOf(value) < 0)
                    warnings.push(value)
            }
        }
        add(report.warnings || draft.warnings || [])
        add(validation.warnings || [])
        return warnings
    }

    function validationLabel() {
        if (!root.hasController())
            return "Not checked"
        if (root.editCommitted)
            return "Applied"
        if (root.agentRunning())
            return "Working · " + root.elapsedLabel()
        if (root.hasRunFailure())
            return "Run failed"
        if (root.wasRunCanceled())
            return "Canceled safely"
        if (root.reviewIssues().length > 0)
            return "Needs review"
        const report = root.agentEditController.lastRunReport || ({})
        const draft = report.draftReport || ({})
        const validation = report.validationReport || draft.validationReport || ({})
        const intentMatch = validation.intentMatch || ({})
        if (Boolean(intentMatch.available))
            return Boolean(intentMatch.ok) ? "Goal matched · " + Number(intentMatch.scorePercent || 100) + "%"
                                           : "Goal not matched"
        if (validation.ok === undefined)
            return "Not checked"
        const score = Number(validation.qualityScorePercent ||
                             Math.round(Number(validation.qualityScore || 0) * 100))
        return Boolean(validation.ok)
                ? "Verified" + (score > 0 ? " · " + score + "%" : "")
                : "Needs review"
    }

    function eventStatusColor(status) {
        const value = String(status || "").toLowerCase()
        if (value === "failed" || value === "rejected")
            return theme.error
        if (value === "warning" || value === "canceled")
            return theme.warning
        if (value === "complete" || value === "ready")
            return theme.success
        return theme.textMuted
    }

    function canReviewDraft() {
        return root.hasController() && root.agentEditController.draftActive && !root.agentRunning()
    }

    function goalMatchPassed() {
        const report = root.lastReport()
        const draft = report.draftReport || ({})
        const validation = report.validationReport || draft.validationReport || ({})
        const intentMatch = validation.intentMatch || ({})
        if (Boolean(intentMatch.available))
            return Boolean(intentMatch.ok)
        return Boolean(validation.ok)
    }

    function acceptDraft() {
        if (root.directionComparisonPending()) {
            root.applyError = "Choose Direction A or Direction B first."
            return
        }
        if (root.canReviewDraft() && root.sampleReviewWaiting()) {
            root.applyError = ""
            root.setPage(2)
            const continued = root.agentEditController.applySampleReviewToAll(true)
            if (!Boolean((continued || ({})).ok)) {
                const errors = (continued || ({})).errors || []
                root.applyError = errors.length > 0 ? errors.join("\n") : "The approved direction could not be applied to the remaining photos."
            }
            if (root.canReviewDraft())
                root.setPage(3)
            return
        }
        if (root.canReviewDraft() && root.reviewIssues().length <= 0) {
            const result = root.agentEditController.commitDraft(true)
            if (Boolean((result || ({})).ok)) {
                root.applyError = ""
                root.committedAssetIds = (result || ({})).assetIds || []
                root.editCommitted = true
                root.editApplied(root.committedAssetIds)
            } else {
                const errors = (result || ({})).errors || []
                root.applyError = errors.length > 0 ? errors.join("\n") : "The edit could not be applied."
            }
        }
    }

    function rejectDraft() {
        if (root.canReviewDraft()) {
            root.agentEditController.rollbackDraft("user_discarded")
            root.close()
        }
    }

    function reviseDraft() {
        if (!root.agentRunning())
            root.setPage(0)
    }

    function reviseSamples() {
        const feedback = root.sampleFeedback.trim()
        if (!root.hasController() || !root.sampleReviewWaiting() || feedback.length === 0)
            return
        root.applyError = ""
        root.setPage(2)
        const result = root.agentEditController.reviseSampleReview(feedback)
        if (!Boolean((result || ({})).ok)) {
            const errors = (result || ({})).errors || []
            root.applyError = errors.length > 0 ? errors.join("\n") : "The samples could not be revised."
        } else {
            root.sampleFeedback = ""
            root.revisionExpanded = false
        }
        if (root.canReviewDraft())
            root.setPage(3)
    }

    function compareDirection() {
        const alternative = root.alternativeDirectionText.trim()
        if (!root.hasController() || !root.canReviewDraft() || alternative.length < 4)
            return
        root.applyError = ""
        const result = root.agentEditController.compareDraftDirection(alternative) || ({})
        if (!Boolean(result.accepted)) {
            const errors = result.errors || []
            root.applyError = errors.length > 0 ? errors.join("\n") : "The alternative direction could not be started."
            return
        }
        root.comparisonPromptOpen = false
        root.alternativeDirectionText = ""
        root.setPage(2)
    }

    function startPhotographerTasteCheck() {
        if (!root.hasController() || !root.canReviewDraft() ||
                typeof root.agentEditController.compareDraftWithPhotographerCalibration !== "function")
            return
        root.applyError = ""
        const result = root.agentEditController.compareDraftWithPhotographerCalibration() || ({})
        if (!Boolean(result.accepted)) {
            const errors = result.errors || []
            root.applyError = errors.length > 0 ? errors.join("\n") :
                                                    "The calibrated alternative could not be started."
            return
        }
        root.comparisonPromptOpen = false
        root.alternativeDirectionText = ""
        root.setPage(2)
    }

    function chooseDirection(key) {
        if (!root.hasController() || !root.directionComparisonPending())
            return
        root.applyError = ""
        const result = root.agentEditController.chooseDraftDirection(key) || ({})
        if (!Boolean(result.ok)) {
            const errors = result.errors || []
            root.applyError = errors.length > 0 ? errors.join("\n") : "That direction could not be selected."
        }
    }

    function resolveReviewIssue(assetId, action) {
        if (!root.hasController() || String(assetId || "").length === 0)
            return
        if (action === "fix")
            root.agentEditController.fixReviewIssue(assetId)
        else if (action === "keep")
            root.agentEditController.acceptReviewIssue(assetId)
        else if (action === "rollback")
            root.agentEditController.rollbackReviewIssue(assetId)
    }

    function fixAllReviewIssues() {
        if (!root.hasController() || root.reviewIssues().length <= 1)
            return
        const result = root.agentEditController.fixAllReviewIssues() || ({})
        if (!Boolean(result.ok)) {
            const errors = result.errors || []
            root.applyError = errors.length > 0 ? errors.join("\n") : "The exceptions could not all be repaired."
        } else {
            root.applyError = ""
        }
    }

    function confirmPendingApproval() {
        if (!root.hasController() || !root.agentEditController.awaitingUserInput)
            return
        root.setPage(2)
        root.agentEditController.answerPendingUserInput("I reviewed the sensitive change and approve creating a reversible draft.")
    }

    function declinePendingApproval() {
        if (root.hasController() && root.agentEditController.awaitingUserInput)
            root.agentEditController.dismissPendingAgentInput()
        root.setPage(1)
    }

    function runButtonLabel() {
        const state = root.workflowState()
        const action = state.primaryAction || ({})
        const key = String(action.key || state.primaryActionKey || "")
        if (key === "create_draft" || key === "create_samples")
            return root.scopeCount(root.scopeMode) >= 3 ? "Create Samples" : "Create Edit"
        return String(action.label || "Create Edit")
    }

    function workflowPrimaryKey() {
        const state = root.workflowState()
        const action = state.primaryAction || ({})
        return String(action.key || state.primaryActionKey || "")
    }

    function triggerPrimaryDirectionAction() {
        const key = root.workflowPrimaryKey()
        if (key === "check_local_model") {
            root.agentEditController.probeSelectedModelAsync(true)
            return
        }
        if (key === "choose_style") {
            root.preparePromptSuggestions()
            return
        }
        if (key === "learn_style" || key === "set_up_local_ai") {
            root.runRequested()
            return
        }
        root.runAgent()
    }

    function runButtonEnabled() {
        const state = root.workflowState()
        if (state.primaryActionEnabled !== undefined)
            return Boolean(state.primaryActionEnabled)
        const action = state.primaryAction || ({})
        return Boolean(action.enabled)
    }

    function directionReady() {
        const key = root.workflowPrimaryKey()
        if (key === "learn_style" || key === "set_up_local_ai" || key === "checking_local_model" || key === "check_local_model")
            return root.runButtonEnabled()
        const instructions = root.hasController()
                ? String(root.agentEditController.userInstructions || "").trim() : ""
        return instructions.length > 0 &&
                (root.inspirationMode !== "references" || root.inspirationReferenceCount() > 0) &&
                root.runButtonEnabled()
    }

    function directionDisabledReason() {
        const instructions = root.hasController()
                ? String(root.agentEditController.userInstructions || "").trim() : ""
        if (instructions.length === 0)
            return "Describe the edit goal first."
        if (root.inspirationMode === "references" && root.inspirationReferenceCount() <= 0)
            return "Choose at least one reference photo or switch to a preset look."
        return root.statusLine()
    }

    function statusLine() {
        const state = root.workflowState()
        if (root.agentRunning()) {
            const events = root.runEvents()
            if (events.length > 0) {
                const detail = String((events[events.length - 1] || ({})).detail || "")
                if (detail.length > 0)
                    return detail
            }
            return String(state.statusLine || "Working")
        }
        if (root.hasController() && String(root.agentEditController.errorMessage || "").length > 0)
            return String(root.agentEditController.errorMessage)
        return String(state.statusLine || (root.agentRunning() ? "Working" : "Ready"))
    }

    function lastReport() {
        return root.hasController() ? root.agentEditController.lastRunReport || ({}) : ({})
    }

    function hasRunFailure() {
        const report = root.lastReport()
        return !root.agentRunning() && !root.canReviewDraft() && report.ok === false &&
                String(report.mode || "") !== "cancel" &&
                String(report.cancellationReason || "").length === 0
    }

    function proofRejectionPresentation() {
        return root.lastReport().proofRejection || ({})
    }

    function wasRunCanceled() {
        const report = root.lastReport()
        return !root.agentRunning() && String(report.cancellationReason || "").length > 0
    }

    function failureDetail() {
        const rejection = root.proofRejectionPresentation()
        if (String(rejection.explanation || "").length > 0)
            return String(rejection.explanation)
        const report = root.lastReport()
        const errors = report.errors || []
        return String(report.failureReason || (errors.length > 0 ? errors[0] : root.statusLine()))
    }

    function friendlyFailureMessage() {
        if (root.hasController() && root.agentEditController.awaitingUserInput)
            return "Review the sensitive change before this reversible draft is created."
        if (root.wasRunCanceled())
            return "The edit was canceled safely. Nothing was applied."
        const rejection = root.proofRejectionPresentation()
        if (String(rejection.headline || "").length > 0)
            return String(rejection.headline)
        return "The edit could not be completed. Nothing was applied."
    }

    function failureHelpText() {
        const detail = root.failureDetail().toLowerCase()
        if (detail.indexOf("recipe json is empty") >= 0)
            return "This unedited photo did not have an edit yet. Looksy can start it from a clean base."
        if (root.hasController() && root.agentEditController.awaitingUserInput)
            return root.pendingApprovalQuestion()
        if (String(root.proofRejectionPresentation().kind || "") ===
                "retouch-quality")
            return "Adjust the target or direction, then I can build and check a new cleanup."
        return "No changes were saved. Change the direction, retry, or open Details for the technical reason."
    }

    function elapsedLabel() {
        const minutes = Math.floor(root.runElapsedSeconds / 60)
        const seconds = root.runElapsedSeconds % 60
        return minutes > 0 ? minutes + "m " + (seconds < 10 ? "0" : "") + seconds + "s"
                           : seconds + "s"
    }

    function resultStateColor() {
        if (root.agentRunning())
            return theme.focusRing
        if (root.hasRunFailure())
            return theme.error
        if (root.wasRunCanceled() || root.runWarnings().length > 0)
            return theme.warning
        if (root.reviewIssues().length > 0)
            return theme.warning
        return theme.success
    }

    function reviewIssueSummary(issue) {
        const guidance = (issue || ({})).repairGuidance || ({})
        const guidedSummary = String(guidance.summary || "")
        if (guidedSummary.length > 0)
            return guidedSummary
        const category = String((issue || ({})).category || "")
        if (category === "corrected_outlier")
            return "I corrected this photo independently because it differed from its cluster."
        const reason = String((issue || ({})).reason || "").toLowerCase()
        if (reason.indexOf("highlight clipping") >= 0)
            return "The highlights are clipping and may have lost visible detail."
        if (reason.indexOf("extreme saturation") >= 0)
            return "The color is unusually saturated and may look unnatural."
        if (reason.indexOf("channel imbalance") >= 0 || reason.indexOf("white balance") >= 0)
            return "The color balance looks unusual and needs a quick decision."
        if (reason.indexOf("histogram") >= 0)
            return "The tonal range looks compressed and needs a quick decision."
        return "This result fell outside the safe quality range."
    }

    function reviewIssuePreservedSummary(issue) {
        const guidance = (issue || ({})).repairGuidance || ({})
        return String(guidance.preservedSummary || "")
    }

    function reviewIssueAutoRepairAvailable(issue) {
        const guidance = (issue || ({})).repairGuidance || ({})
        return String(guidance.contract || "").length === 0 || Boolean(guidance.autoRepairSafe)
    }

    function reviewIssueFixLabel(issue) {
        const guidance = (issue || ({})).repairGuidance || ({})
        return String(guidance.actionLabel || "Fix")
    }

    function reviewIssueTitle(issue) {
        const item = issue || ({})
        const label = String(item.displayName || item.assetId || "")
        if (label.length === 0 || label.indexOf("asset-") === 0)
            return "Edited photo"
        return label
    }

    function resultTitle() {
        if (root.editCommitted)
            return "Edit applied"
        if (root.agentRunning())
            return "Looksy is working"
        if (root.hasRunFailure())
            return String(root.proofRejectionPresentation().headline ||
                          "Run needs attention")
        if (root.wasRunCanceled())
            return "Edit canceled"
        if (root.canReviewDraft() && root.sampleReviewWaiting())
            return "Review your samples"
        return root.canReviewDraft() ? "Review your edit" : "Edit complete"
    }

    function retryAgent() {
        if (root.agentRunning())
            return
        root.setPage(2)
        root.runRequested()
    }

    function useFastFallback() {
        if (!root.hasController() || root.agentRunning())
            return
        root.agentEditController.selectedModelId = "rulebook-deterministic"
        root.setPage(2)
        root.runRequested()
    }

    function selectedTargetText() {
        const selectedCount = root.scopeCount("selected-assets")
        if (root.scopeMode === "selected-assets" && selectedCount > 0)
            return selectedCount + " selected photo" + (selectedCount === 1 ? "" : "s")
        if (root.scopeMode === "current-photo")
            return "Current Photo"
        return root.projectLabel()
    }

    function activityRows() {
        const events = root.runEvents()
        if (events.length === 0)
            return [{ label: "Ready", status: "pending", detail: "Waiting to start." }]
        const latestByStage = ({})
        const stageOrder = []
        for (let index = 0; index < events.length; ++index) {
            const event = events[index] || ({})
            const stage = String(event.stage || event.label || index)
            if (latestByStage[stage] === undefined)
                stageOrder.push(stage)
            latestByStage[stage] = event
        }
        return stageOrder.slice(Math.max(0, stageOrder.length - 8)).map(function(stage) {
            return latestByStage[stage]
        })
    }

    function activityTitle(event) {
        const stage = String((event || ({})).stage || "")
        if (stage === "connect") return "Prepare Looksy"
        if (stage === "observe") return "Inspect the edit target"
        if (stage === "plan") return "Choose photo adjustments"
        if (stage === "fallback") return "Use the safe edit path"
        if (stage === "constrain") return "Check safety and limits"
        if (stage === "act") return "Apply adjustments to a draft"
        if (stage === "render") return "Render an edited preview"
        if (stage === "validate") return "Check the rendered image"
        if (stage === "model-review") return "Review the visual result"
        if (stage === "repair") return "Refine the draft"
        return String((event || ({})).label || "Prepare the edit")
    }

    function activityDescription(event) {
        const stage = String((event || ({})).stage || "")
        if (stage === "connect") return "Preparing the editing service."
        if (stage === "observe") return "Reading the selected photo and its current edits."
        if (stage === "plan") return "Choosing adjustments that match your direction."
        if (stage === "fallback") return "Switching to a reliable local edit path."
        if (stage === "constrain") return "Checking the requested changes and permissions."
        if (stage === "act") return "Applying adjustments to a reversible draft."
        if (stage === "render") return "Rendering the actual edited preview."
        if (stage === "validate") return "Comparing the preview with your requested result."
        if (stage === "model-review") return "Reviewing the rendered visual result."
        if (stage === "repair") return "Refining the draft after review."
        return "Preparing the edit."
    }

    function technicalActivityDescription(event) {
        const detail = String((event || ({})).detail || "")
        return detail.length > 0 ? detail : root.activityDescription(event)
    }

    function targetPreviewSource() {
        const assets = root.targetAssets()
        if (assets.length <= 0)
            return ""
        return root.normalizeImageSource(String((assets[0] || ({})).thumbnailSource || ""))
    }

    function livePreviewSource() {
        if (root.proofImages.length > 0)
            return String((root.proofImages[root.proofImages.length - 1] || ({})).source || "")
        return root.targetPreviewSource()
    }

    function livePreviewLabel() {
        if (root.proofImages.length > 0)
            return String((root.proofImages[root.proofImages.length - 1] || ({})).label || "Latest edited preview")
        return root.agentRunning() ? "Original target · preview appears after rendering"
                                   : "Target unchanged · no edited preview was produced"
    }

    function proofSourceForLabel(labelText) {
        const wanted = String(labelText || "").toLowerCase()
        for (let index = 0; index < root.proofImages.length; ++index) {
            const proof = root.proofImages[index] || ({})
            if (String(proof.label || "").toLowerCase().indexOf(wanted) >= 0)
                return String(proof.source || "")
        }
        return ""
    }

    function reviewBeforeSource() {
        if (root.hasDirectionComparison())
            return root.directionProofSource("a")
        if (root.sampleReviewWaiting()) {
            const sample = root.displayedSample()
            if (String(sample.beforeProofPath || "").length > 0)
                return root.normalizeImageSource(String(sample.beforeProofPath))
        }
        const before = root.proofSourceForLabel("before")
        return before.length > 0 ? before : root.targetPreviewSource()
    }

    function reviewAfterSource() {
        if (root.hasDirectionComparison())
            return root.directionProofSource("b")
        if (root.sampleReviewWaiting()) {
            const sample = root.displayedSample()
            if (String(sample.afterProofPath || "").length > 0)
                return root.normalizeImageSource(String(sample.afterProofPath))
        }
        return root.proofSourceForLabel("after")
    }

    function reviewProofContextLabel() {
        if (root.sampleReviewWaiting())
            return "Actual rendered proof · sample " +
                    (root.displayedSampleIndex() + 1) + " of " + root.sampleCount()
        return root.affectedPhotoSummary()
    }

    function planSummary() {
        const report = root.lastReport()
        const draft = report.draftReport || ({})
        return String(report.planSummary || draft.planSummary || "The draft was rendered and checked against the requested goal.")
    }

    function editIntentSummary() {
        const report = root.lastReport()
        const draft = report.draftReport || ({})
        const intent = report.editIntent || draft.editIntent || ({})
        const parts = []
        if (String(intent.colorMode || "preserve") === "monochrome") parts.push("Black & white")
        if (String(intent.contrast || "natural") === "strong") parts.push("Strong contrast")
        if (String(intent.brightness || "preserve") === "brighter") parts.push("Brighter")
        if (String(intent.brightness || "preserve") === "darker") parts.push("Darker")
        if (String(intent.palette || "preserve") === "warm") parts.push("Warm palette")
        if (String(intent.palette || "preserve") === "cool") parts.push("Cool palette")
        if (String(intent.palette || "preserve") === "reference") parts.push("Match reference")
        if (String(intent.crop || "preserve") === "improve") parts.push("Improve crop")
        if (String(intent.crop || "preserve") === "tighter") parts.push("Tighter crop")
        if (String(intent.detail || "natural") === "soft") parts.push("Softer detail")
        if (String(intent.detail || "natural") === "crisp") parts.push("Crisper detail")
        return parts.length > 0 ? parts.join(" · ") : "Natural tone, color, and detail"
    }

    function displayThinkingLines() {
        if (root.hasRunFailure())
            return [root.friendlyFailureMessage(), root.failureDetail()]
        if (root.wasRunCanceled())
            return [root.friendlyFailureMessage()]
        const events = root.runEvents()
        if (events.length > 0) {
            const event = events[events.length - 1] || ({})
            const detail = String(event.detail || "")
            if (detail.length > 0)
                return [detail]
        }
        const lines = root.thinkingLines || []
        if (lines.length > 0)
            return lines.slice(0, 3)
        const status = root.statusLine()
        return [status.length > 0 ? status : "Looksy is ready."]
    }

    Rectangle {
        id: terminalDimmer
        anchors.fill: parent
        color: theme.ink950
        opacity: root.presentationProgress * 0.72
    }

    Rectangle {
        id: terminalFrame
        anchors.fill: parent
        anchors.margins: 18
        radius: 6
        color: theme.ink950
        border.width: 1
        border.color: theme.lineStrong
        clip: true
        opacity: root.presentationProgress
        scale: 0.965 + root.presentationProgress * 0.035
        y: (1 - root.presentationProgress) * 18

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            color: theme.ink900
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 18

                ColumnLayout {
                    Layout.preferredWidth: 150
                    spacing: 1
                    BrandWordmark {
                        Layout.preferredWidth: implicitWidth
                        Layout.preferredHeight: implicitHeight
                        compact: true
                        showMark: true
                        showDescriptor: false
                    }
                    Label {
                        text: "AGENT TERMINAL  /  01"
                        color: theme.brandGreen
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        font.letterSpacing: 1.35
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignCenter
                    spacing: 8

                    Repeater {
                        model: [
                            { title: "Direction", page: 0 },
                            { title: "Inspiration", page: 1 },
                            { title: "Working", page: 2 },
                            { title: "Review", page: 3 }
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool selected: root.currentPage === Number(modelData.page)
                            readonly property bool completed: root.currentPage > Number(modelData.page)
                            implicitWidth: stageLabel.implicitWidth + 24
                            height: 34
                            radius: 2
                            color: selected ? theme.ink750 : "transparent"
                            border.width: selected ? 1 : 0
                            border.color: theme.lineSoft

                            Label {
                                id: stageLabel
                                anchors.centerIn: parent
                                text: modelData.title
                                color: parent.selected ? theme.textStrong
                                                       : parent.completed ? theme.textMain : theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 10
                                font.letterSpacing: 0.5
                                font.weight: parent.selected ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }

                Button {
                    objectName: "agentReturnToEditorButton"
                    text: "Back to editor"
                    enabled: !root.agentRunning()
                    implicitWidth: 142
                    implicitHeight: 40
                    onClicked: root.close()
                    background: Rectangle {
                        radius: 2
                        color: parent.hovered ? theme.ink750 : theme.ink850
                        border.width: 1
                        border.color: theme.lineSoft
                    }
                    contentItem: Label {
                        text: parent.text
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        AgentConversationView {
            id: conversationView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Math.max(24, (root.width - 1320) * 0.5)
            Layout.rightMargin: Math.max(24, (root.width - 1320) * 0.5)
            Layout.topMargin: 20
            Layout.bottomMargin: 20
        dialog: root
        themeObject: theme
        }
        }
    }
}
