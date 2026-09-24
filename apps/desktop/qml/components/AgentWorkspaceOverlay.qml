pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    FileDialog {
        id: photographerCalibrationDialog
        title: "Choose Photographer Calibration"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Photographer calibration (*.json)", "JSON files (*.json)"]
        onAccepted: root.importPhotographerCalibration(selectedFile)
    }

    FileDialog {
        id: photographerTastePassportImportDialog
        title: "Choose Taste Passport"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Taste Passport (*.json)", "JSON files (*.json)"]
        onAccepted: root.importPhotographerTastePassport(selectedFile)
    }

    FileDialog {
        id: photographerTastePassportExportDialog
        title: "Save Taste Passport"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["Taste Passport (*.json)", "JSON files (*.json)"]
        onAccepted: root.exportPhotographerTastePassport(selectedFile)
    }

    FileDialog {
        id: photographerCalibrationReviewDialog
        title: "Choose Blind Calibration Review"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Calibration review (index.html review-contract.json)",
                      "HTML files (*.html)", "JSON files (*.json)"]
        onAccepted: {
            if (root.calibrationFinalizePending) {
                root.finalizePhotographerCalibration(
                            root.pendingCalibrationRatingsUrl, selectedFile)
                root.calibrationFinalizePending = false
                root.pendingCalibrationRatingsUrl = ""
                return
            }
            root.openPhotographerCalibrationReview(selectedFile)
        }
        onRejected: {
            if (root.calibrationFinalizePending) {
                root.calibrationFinalizePending = false
                root.pendingCalibrationRatingsUrl = ""
            }
        }
    }

    FileDialog {
        id: photographerCalibrationRatingsDialog
        title: "Choose Exported Photographer Ratings"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Photographer ratings (*.json)", "JSON files (*.json)"]
        onAccepted: root.finishPhotographerCalibrationRatings(selectedFile)
    }

    property var agentEditController: null
    property var catalogModel: null
    property var targetAssetIds: []
    property string prompt: ""
    property int inspectorWidth: 340
    property int filmstripHeight: 88
    property bool reducedMotion: false
    property bool applyInProgress: false
    property var activeEvent: ({})
    property int eventSerial: 0
    property bool traveling: false
    property bool speaking: false
    property bool correctionOpen: false
    property string correctionText: ""
    property string correctionError: ""
    property string pendingAnswerText: ""
    property string pendingAnswerError: ""
    property string activeFindingMaskId: ""
    property var activeFinding: ({})
    property bool tasteMirrorOpen: false
    property bool tasteRenameOpen: false
    property string tasteRenameText: ""
    property bool northStarEditOpen: false
    property string northStarDraftText: ""
    property bool northStarForgetArmed: false
    property string tasteForgetPendingKind: ""
    property string tasteForgetPendingId: ""
    property bool twoSpeedTasteForgetArmed: false
    property string assignmentReflectionDismissedProjectKey: ""
    property string tasteActionMessage: ""
    property string tasteCheckMessage: ""
    property bool calibrationFinalizePending: false
    property string pendingCalibrationRatingsUrl: ""
    property string pendingTastePassportUrl: ""
    property string pendingTastePassportDigest: ""
    property var pendingTastePassportPreview: ({})
    property string activeCalibrationReviewPacketUrl: ""
    property string activeCalibrationReviewPacketDigest: ""
    property string activeCalibrationReviewProjectKey: ""
    property int intentBoundarySerial: 0
    property int localPlanningElapsedSeconds: 0
    property bool spatialTargetMode: false
    property bool spatialTargetChosen: false
    property var spatialTargetPoint: ({})
    property string spatialTargetAssetId: ""
    property bool rejectedRepairPointMode: false

    signal assetRequested(string assetId)
    signal reopenRequested()
    signal draftCommitted(var report)
    signal findingRequested(var finding)
    signal spatialTargetModeRequested(bool enabled)

    readonly property bool hasController: agentEditController !== null && agentEditController !== undefined
    readonly property bool running: applyInProgress || (hasController && Boolean(agentEditController.running))
    readonly property bool draftReady: hasController && Boolean(agentEditController.draftActive) && !running
    readonly property var progressivePreview: hasController
                                               ? (agentEditController.progressivePreview || ({}))
                                               : ({})
    readonly property bool progressivePreviewVisible: running &&
                                                       Boolean(progressivePreview.published) &&
                                                       Boolean(progressivePreview.validationPending)
    readonly property bool fastPreviewValidated: progressivePreviewVisible &&
                                                  Boolean(progressivePreview.fastValidationPassed)
    readonly property bool keepValidatedFastAvailable: running &&
                                                       fastPreviewValidated &&
                                                       Boolean(progressivePreview.keepFastDraftAvailable)
    readonly property bool awaitingInput: hasController && Boolean(agentEditController.awaitingUserInput)
    readonly property var pendingReport: hasController
                                         ? (agentEditController.lastRunReport || ({}))
                                         : ({})
    readonly property var twoSpeedDraftChoice: pendingReport.twoSpeedDraftChoice || ({})
    readonly property bool twoSpeedDraftChoiceAvailable: draftReady &&
                                                          String(twoSpeedDraftChoice.contract || "") ===
                                                          "agent-two-speed-draft-choice-v1" &&
                                                          Boolean(twoSpeedDraftChoice.available)
    readonly property string selectedTwoSpeedDraftVersion:
        String(twoSpeedDraftChoice.currentVersion || "deep-review")
    readonly property int protectedIntentCount:
        (pendingReport.protectedDraftIntents || []).length
    readonly property string pendingInputKind: String(pendingReport.inputKind || "")
    readonly property var pendingQuestions: hasController
                                            ? (agentEditController.pendingUserQuestions || [])
                                            : []
    readonly property var selectionConfirmation: hasController
                                                 ? ((agentEditController.lastRunReport || ({})).selectionConfirmation || ({}))
                                                 : ({})
    readonly property bool awaitingSelectionConfirmation: awaitingInput &&
                                                           String(selectionConfirmation.contract || "") ===
                                                           "agent-grounded-selection-confirmation-v1" &&
                                                           Boolean(selectionConfirmation.required)
    readonly property bool awaitingPlanApproval: awaitingInput &&
                                                  pendingInputKind === "pre_draft_plan_approval"
    readonly property bool awaitingClarification: awaitingInput &&
                                                  !awaitingSelectionConfirmation &&
                                                  !awaitingPlanApproval &&
                                                  pendingQuestions.length > 0
    readonly property bool awaitingSafeStop: awaitingInput &&
                                             !awaitingSelectionConfirmation &&
                                             !awaitingPlanApproval &&
                                             !awaitingClarification
    readonly property bool runFailed: hasController && !running && !draftReady && !awaitingInput &&
                                      Boolean((agentEditController.lastRunReport || ({})).ok === false)
    readonly property bool localModelPlanningActive: running &&
                                                      String(activeEvent.stage || "").indexOf("plan-worker") === 0 &&
                                                      String(activeEvent.status || "") === "running"
    readonly property var eventEvidence: activeEvent.evidence || ({})
    readonly property string activeAssetId: String(eventEvidence.assetId || "")
    readonly property int activeTargetIndex: Math.max(0, targetAssetIds.indexOf(activeAssetId))
    readonly property string uiAnchor: String(activeEvent.uiAnchor || "image")
    readonly property real agentTargetX: uiAnchor === "inspector"
                                               ? Math.max(80, width - inspectorWidth - 112)
                                               : uiAnchor === "filmstrip"
                                                 ? Math.max(80, width * 0.56)
                                                 : uiAnchor === "toolbar"
                                                   ? Math.max(80, width * 0.68)
                                                   : uiAnchor === "compare"
                                                     ? Math.max(80, width * 0.58)
                                                     : Math.max(80, width * 0.43)
    readonly property real agentTargetY: uiAnchor === "filmstrip"
                                               ? Math.max(80, height - filmstripHeight - 78)
                                               : uiAnchor === "toolbar"
                                                 ? 88
                                                 : uiAnchor === "inspector"
                                                   ? Math.max(120, height * 0.48)
                                                   : Math.max(120, height * 0.58)

    Theme { id: theme }

    onDraftReadyChanged: {
        if (!root.draftReady && !root.running) {
            root.correctionOpen = false
            root.correctionText = ""
            root.correctionError = ""
            root.activeFindingMaskId = ""
            root.activeFinding = ({})
            root.spatialTargetChosen = false
            root.spatialTargetPoint = ({})
            root.spatialTargetAssetId = ""
            root.tasteCheckMessage = ""
            root.spatialTargetModeRequested(false)
        }
    }

    onAwaitingInputChanged: {
        if (!root.awaitingInput) {
            root.pendingAnswerText = ""
            root.pendingAnswerError = ""
        }
    }

    function pendingQuestionText() {
        if (root.pendingQuestions.length <= 0)
            return ""
        const first = root.pendingQuestions[0]
        if (typeof first === "string")
            return first
        const question = first || ({})
        return String(question.question || question.prompt || question.summary || "")
    }

    function proofRejectionPresentation() {
        if (!root.hasController)
            return ({})
        return (root.agentEditController.lastRunReport || ({})).proofRejection || ({})
    }

    function visualCritique() {
        const rejection = root.proofRejectionPresentation()
        return rejection.visualCritique || ({})
    }

    function beginRejectedRepair() {
        const critique = root.visualCritique()
        if (!root.runFailed || String(critique.contract || "") !==
                "agent-visual-critique-v1")
            return false
        const assetId = String(critique.assetId ||
                               root.proofRejectionPresentation().assetId || "")
        if (assetId.length > 0)
            root.assetRequested(assetId)
        root.rejectedRepairPointMode = true
        root.correctionError = ""
        root.spatialTargetChosen = false
        root.spatialTargetPoint = ({})
        root.spatialTargetAssetId = assetId
        root.spatialTargetModeRequested(true)
        return true
    }

    function submitPendingAnswer() {
        const answer = root.pendingAnswerText.trim()
        if (!root.hasController || answer.length === 0)
            return
        root.pendingAnswerError = ""
        const report = root.agentEditController.answerPendingUserInput(answer) || ({})
        if (Boolean(report.ok) || String(report.errorCode || "") === "async_stage_pending" ||
                !root.awaitingInput) {
            root.pendingAnswerText = ""
            return
        }
        const errors = report.errors || []
        root.pendingAnswerError = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't continue safely with that answer."))
    }

    function submitCorrection() {
        const feedback = root.correctionText.trim()
        if (!root.hasController || feedback.length === 0)
            return
        root.correctionError = ""
        const report = root.spatialTargetChosen
                ? root.agentEditController.reviseDraftAtPoint(
                      Number(root.spatialTargetPoint.x),
                      Number(root.spatialTargetPoint.y),
                      feedback,
                      root.spatialTargetAssetId) || ({})
                : root.activeFindingMaskId.length > 0
                ? root.agentEditController.reviseDraftFinding(root.activeFinding, feedback) || ({})
                : root.agentEditController.reviseDraft(feedback) || ({})
        if (Boolean(report.ok)) {
            if (root.spatialTargetChosen) {
                const spatial = report.pointGroundedCorrection || ({})
                root.spatialTargetModeRequested(false)
                if (Boolean(spatial.applied))
                    root.findingRequested(spatial)
            }
            root.correctionOpen = false
            root.correctionText = ""
            root.spatialTargetChosen = false
            root.spatialTargetPoint = ({})
            root.spatialTargetAssetId = ""
        } else {
            const errors = report.errors || []
            root.correctionError = String(report.failureReason ||
                                          (errors.length > 0 ? errors[0]
                                                             : "That correction could not be applied safely."))
        }
    }

    function beginSpatialCorrection() {
        if (!root.draftReady || root.protectedIntentCount > 0)
            return false
        root.correctionOpen = false
        root.correctionText = ""
        root.correctionError = ""
        root.activeFindingMaskId = ""
        root.activeFinding = ({})
        root.spatialTargetChosen = false
        root.spatialTargetPoint = ({})
        root.spatialTargetAssetId = ""
        root.spatialTargetModeRequested(true)
        return true
    }

    function cancelSpatialCorrection() {
        root.rejectedRepairPointMode = false
        root.spatialTargetChosen = false
        root.spatialTargetPoint = ({})
        root.spatialTargetAssetId = ""
        root.correctionOpen = false
        root.correctionText = ""
        root.correctionError = ""
        root.spatialTargetModeRequested(false)
    }

    function acceptSpatialTarget(normalizedX, normalizedY, assetId) {
        const x = Number(normalizedX)
        const y = Number(normalizedY)
        if (!Number.isFinite(x) || !Number.isFinite(y) ||
                x < 0 || x > 1 || y < 0 || y > 1)
            return false
        if (root.rejectedRepairPointMode) {
            const repairAssetId = String(assetId || root.spatialTargetAssetId || "")
            const report = root.agentEditController.prepareRejectedRetouchRepairAtPoint(
                               x, y, repairAssetId) || ({})
            root.spatialTargetModeRequested(false)
            root.rejectedRepairPointMode = false
            root.spatialTargetChosen = false
            root.spatialTargetPoint = ({})
            root.spatialTargetAssetId = ""
            if (Boolean(report.needsUserInput) &&
                    String(report.inputKind || "") === "pre_draft_plan_approval") {
                root.correctionError = ""
                return true
            }
            const errors = report.errors || []
            root.correctionError = String(report.failureReason ||
                                          (errors.length > 0 ? errors[0]
                                                             : "That repair target could not be prepared safely."))
            return false
        }
        root.spatialTargetPoint = ({ x: x, y: y })
        root.spatialTargetAssetId = String(assetId || "")
        root.spatialTargetChosen = true
        root.activeFindingMaskId = ""
        root.activeFinding = ({
            target: "pointed-area",
            targetLabel: "Pointed area",
            assetId: root.spatialTargetAssetId,
            region: {
                centerX: x,
                centerY: y,
                radiusX: 0.12,
                radiusY: 0.12,
                feather: 0.72
            }
        })
        root.correctionOpen = true
        root.correctionError = ""
        Qt.callLater(function() { correctionEditor.forceActiveFocus() })
        return true
    }

    component BubbleButton: Button {
        id: bubbleButton
        property bool primary: false
        implicitHeight: 34
        leftPadding: 13
        rightPadding: 13
        opacity: enabled ? 1.0 : 0.42
        background: Rectangle {
            radius: 9
            color: bubbleButton.primary
                   ? (bubbleButton.down ? Qt.darker(theme.primaryAccent, 1.08) : theme.primaryAccent)
                   : (bubbleButton.hovered ? theme.alpha(theme.brandInk, 0.13)
                                           : theme.alpha(theme.brandInk, 0.075))
        }
        contentItem: Label {
            text: bubbleButton.text
            color: bubbleButton.primary ? theme.brandPaper : theme.brandInk
            font.family: theme.fontUi
            font.pixelSize: 11
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component LedgerButton: Button {
        id: ledgerButton
        property bool accent: false
        implicitHeight: 24
        implicitWidth: ledgerButtonLabel.implicitWidth + 16
        leftPadding: 8
        rightPadding: 8
        opacity: enabled ? 1.0 : 0.38
        background: Rectangle {
            radius: 7
            color: ledgerButton.accent
                   ? theme.alpha(theme.primaryAccent, ledgerButton.hovered ? 0.22 : 0.16)
                   : ledgerButton.hovered ? theme.alpha(theme.primaryAccent, 0.16)
                                          : theme.alpha(theme.brandInk, 0.065)
            border.width: 1
            border.color: theme.alpha(theme.primaryAccent,
                                      ledgerButton.accent ? 0.46 : 0.18)
        }
        contentItem: Label {
            id: ledgerButtonLabel
            text: ledgerButton.text
            color: theme.primaryAccent
            font.family: theme.fontUi
            font.pixelSize: 9
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    function eventList() {
        return root.hasController ? root.agentEditController.runEvents || [] : []
    }

    function latestEvent() {
        const events = root.eventList()
        if (events.length <= 0)
            return ({
                headline: root.draftReady ? "Your edit is ready." : "I'm getting started.",
                explanation: root.draftReady
                             ? "Look at the actual photo here before you apply anything."
                             : "I'll show you what I'm working on as I move through the photos.",
                uiAnchor: "image",
                characterMode: root.draftReady ? "success" : "thinking",
                characterTool: root.draftReady ? "none" : "sampler"
            })
        return events[events.length - 1] || ({})
    }

    function refreshActiveEvent() {
        const previousAnchor = root.uiAnchor
        root.activeEvent = root.latestEvent()
        root.eventSerial += 1
        const evidence = root.activeEvent.evidence || ({})
        const assetId = String(evidence.assetId || "")
        if (assetId.length > 0)
            root.assetRequested(assetId)
        const nextAnchor = root.uiAnchor
        if (!root.reducedMotion && previousAnchor !== nextAnchor) {
            root.speaking = false
            root.traveling = true
            travelArrival.restart()
        } else {
            root.traveling = false
            root.finishArrival()
        }
    }

    function finishArrival() {
        root.speaking = true
        speechTimer.restart()
        speechArrival.restart()
        const mode = root.assistantLightMode()
        if (mode === "adjust" || mode === "point")
            touchPulse.restart()
    }

    function headline() {
        if (root.spatialTargetMode)
            return root.rejectedRepairPointMode
                    ? "Point to the center of the repair."
                    : "Point to the exact area."
        if (root.awaitingSelectionConfirmation)
            return "Show me which one you mean."
        if (root.awaitingPlanApproval)
            return "I need your approval before I continue."
        if (root.awaitingClarification)
            return "I need one detail from you."
        if (root.awaitingSafeStop)
            return "I stopped before changing anything."
        if (root.draftReady)
            return "Your edit is ready."
        if (root.runFailed) {
            const rejection = root.proofRejectionPresentation()
            return String(rejection.headline || "I couldn't finish this edit safely.")
        }
        if (root.progressivePreviewVisible)
            return String(root.progressivePreview.mode || "") === "fast-planning-preview"
                    ? (root.keepValidatedFastAvailable
                       ? "The fast draft is proved."
                       : root.fastPreviewValidated
                         ? "The first look passed its proof."
                         : "You already have a first look.")
                    : "The draft is already on your photo."
        return String(root.activeEvent.headline || root.activeEvent.label || "I'm working on the edit.")
    }

    function explanation() {
        if (root.spatialTargetMode) {
            if (root.rejectedRepairPointMode) {
                const repair = root.visualCritique().repair || ({})
                return String(repair.instruction ||
                              "Click once on the distraction. I’ll tighten the rejected target and ask for approval again before rebuilding it.")
            }
            return "Click once on the photo. I’ll follow local content boundaries when the evidence is clear; otherwise the visible feathered area stays the exact target."
        }
        if (root.awaitingSelectionConfirmation)
            return "I found more than one plausible " +
                    String(root.selectionConfirmation.targetLabel || "object") +
                    ". I haven't changed the photo yet."
        if (root.awaitingClarification)
            return root.pendingQuestionText()
        if (root.awaitingPlanApproval) {
            const question = root.pendingQuestionText()
            return question.length > 0
                    ? question
                    : "Review the sensitive change, then approve the exact plan or dismiss it."
        }
        if (root.awaitingSafeStop)
            return String(root.pendingReport.safeStopReason ||
                          "This request is outside the active safety or style rules.")
        if (root.draftReady)
            return "Inspect the real result here. Commit it, make changes, or discard it."
        if (root.runFailed) {
            const rejection = root.proofRejectionPresentation()
            if (String(rejection.explanation || "").length > 0)
                return String(rejection.explanation)
            const report = root.agentEditController.lastRunReport || ({})
            const errors = report.errors || []
            return String(report.failureReason || (errors.length > 0 ? errors[0]
                                                                : "No changes were committed. Change the direction or try again."))
        }
        if (root.progressivePreviewVisible)
            return String(root.progressivePreview.mode || "") === "fast-planning-preview"
                    ? (root.keepValidatedFastAvailable
                       ? "This fast draft passed its own image proof. Keep it now as a reversible draft, or wait for deep local vision; neither choice commits the edit."
                       : root.fastPreviewValidated
                         ? "This reversible first look passed its own image proof. " +
                           root.fastDraftDeepTargetCountdownText() +
                           "; deep local vision is still refining it, and Commit stays locked."
                         : "The fast local planner made this reversible preview. Its own image proof and deep local vision are running together; Commit stays locked.")
                    : "Your preview is ready. I’m finishing the safety check; Commit Edits unlocks when it passes."
        return String(root.activeEvent.explanation || root.activeEvent.detail || "")
    }

    function assetLabel() {
        if (root.targetAssetIds.length <= 0)
            return "Working in Edit"
        const current = Math.min(root.targetAssetIds.length, root.activeTargetIndex + 1)
        return "Photo " + current + " of " + root.targetAssetIds.length
    }

    function fastDraftDeepTargetCountdownSeconds() {
        if (!root.fastPreviewValidated ||
                Boolean(root.progressivePreview.deepDeadlineReached))
            return 0
        const policy = root.progressivePreview.hardwareDeadline || ({})
        const deadlineMs = Math.max(0, Number(root.progressivePreview.deepModelDeadlineMs ||
                                              policy.deepModelDeadlineMs || 0))
        if (deadlineMs <= 0)
            return 0
        const elapsedMs = Math.max(0, root.localPlanningElapsedSeconds * 1000)
        return Math.max(1, Math.ceil((deadlineMs - elapsedMs) / 1000))
    }

    function fastDraftDeepTargetCountdownText() {
        const seconds = root.fastDraftDeepTargetCountdownSeconds()
        return seconds > 0 ? "DEEP REVIEW TARGET " + String(seconds) + "s" : ""
    }

    function localPlanningReceiptText() {
        const elapsed = Math.max(0, root.localPlanningElapsedSeconds)
        const minutes = Math.floor(elapsed / 60)
        const seconds = String(elapsed % 60).padStart(2, "0")
        const phase = elapsed < 15
                      ? "STARTING"
                      : elapsed < 60
                        ? "READING PHOTO"
                        : elapsed < 90
                          ? "BUILDING PLAN"
                          : "DEEP REVIEW"
        const countdown = root.fastDraftDeepTargetCountdownText()
        const previewReceipt = root.progressivePreviewVisible
                               ? (root.keepValidatedFastAvailable
                                  ? " · PROVED PREVIEW · USE NOW OR WAIT" +
                                    (countdown.length > 0 ? " · " + countdown : "")
                                  : root.fastPreviewValidated
                                    ? " · PROVED PREVIEW · " + countdown
                                    : " · REVERSIBLE PREVIEW LIVE · PROVING")
                               : " · NO PIXELS CHANGED"
        return "LOCAL VISION · " + phase + " · " + minutes + ":" + seconds +
               previewReceipt + " · CANCEL SAFE"
    }

    function trustReceiptText() {
        if (!root.hasController || !root.draftReady)
            return ""
        const report = root.agentEditController.lastRunReport || ({})
        const batch = report.batchSummary || ({})
        const assetReports = report.assetReports || []
        const firstAsset = assetReports.length > 0 ? assetReports[0] || ({}) : ({})
        const semanticSelection = firstAsset.groundedSelection ||
                                  firstAsset.semanticSelection || ({})
        const validation = report.validationReport || firstAsset.validationReport || ({})
        const artifacts = report.artifacts || validation.artifacts || ({})
        const proofCount = Math.max(Number(batch.proofCount || 0),
                                    String(report.currentProofPath || report.afterProofPath ||
                                           artifacts.currentProofPath || artifacts.afterProofPath || "").length > 0 ? 1 : 0)
        const score = Number(validation.qualityScorePercent ||
                             Math.round(Number(validation.qualityScore || firstAsset.qualityScore || 0) * 100))
        const targetedCorrection = report.targetedCorrection || ({})
        const pointGroundedCorrection = report.pointGroundedCorrection || ({})
        const visualReferent = report.visualReferentResolution || ({})
        const parts = []
        if (proofCount > 0)
            parts.push(proofCount + " real preview" + (proofCount === 1 ? "" : "s") + " checked")
        const generatedSemanticCount = Number(semanticSelection.generatedCount || 0)
        const explicitSemanticCount = Number(semanticSelection.semanticGeneratedCount || 0)
        const guidedSelectionCount = Number(semanticSelection.guidedGeneratedCount || 0)
        const semanticFallbackCount = Number(semanticSelection.fallbackCount || 0)
        const semanticReceiptCount = explicitSemanticCount > 0 || guidedSelectionCount > 0
                ? explicitSemanticCount : generatedSemanticCount
        if (semanticReceiptCount > 0 || guidedSelectionCount > 0)
            parts.push("local target boundary checked")
        if (semanticFallbackCount > 0)
            parts.push("visible feathered target kept")
        if (score > 0)
            parts.push("goal matched " + score + "%")
        if (Boolean(targetedCorrection.applied)) {
            const targetLabel = String(targetedCorrection.targetLabel || "selected finding")
            parts.push("only " + targetLabel + " revised")
        }
        if (Boolean(pointGroundedCorrection.applied)) {
            if (String(pointGroundedCorrection.resolutionMode || "") === "content-snap")
                parts.push("snapped to local content · outside it protected")
            else
                parts.push("feathered fallback · no object claim")
        }
        if (Boolean(visualReferent.resolved)) {
            const chosenLabel = String(visualReferent.chosenLabel ||
                                       visualReferent.targetLabel || "confirmed object")
            if (Boolean(visualReferent.fastPathUsed))
                parts.push("used your confirmed " + chosenLabel)
        }
        return parts.join(" · ")
    }

    function draftEditIntent() {
        if (!root.hasController)
            return ({})
        const report = root.agentEditController.lastRunReport || ({})
        return report.editIntent || ({})
    }

    function visualFindings() {
        const intent = root.draftEditIntent()
        return intent.visualFindings || []
    }

    function localizedActions() {
        const intent = root.draftEditIntent()
        return intent.localizedActions || []
    }

    function editStrategy() {
        if (!root.hasController)
            return ({})
        const report = root.agentEditController.lastRunReport || ({})
        return report.editStrategy || ({})
    }

    function editStrategyPreservedText() {
        const preserved = root.editStrategy().preserved || []
        if (preserved.length === 0)
            return ""
        return "PROTECTED · " + preserved.slice(0, 4).join(" · ")
    }

    function findingForLocalizedAction(action) {
        const resolvedAction = action || ({})
        const maskId = String(resolvedAction.maskId || "")
        const findings = root.visualFindings()
        for (let index = 0; index < findings.length; ++index) {
            const finding = findings[index] || ({})
            if (maskId.length > 0 && String(finding.maskId || "") === maskId)
                return finding
        }
        return resolvedAction
    }

    function adjustmentLabel(key) {
        const labels = {
            exposure: "Exposure",
            highlights: "Highlights",
            shadows: "Shadows",
            contrast: "Contrast",
            temperature: "Warmth",
            tint: "Tint",
            saturation: "Saturation",
            clarity: "Clarity"
        }
        return String(labels[String(key)] || String(key).replace(/_/g, " "))
    }

    function localizedActionSummary(action) {
        const adjustments = (action || ({})).adjustments || ({})
        const keys = Object.keys(adjustments).sort()
        const parts = []
        for (let index = 0; index < Math.min(2, keys.length); ++index) {
            const key = keys[index]
            const value = Number(adjustments[key] || 0)
            const rounded = Math.round(value * 100) / 100
            parts.push(root.adjustmentLabel(key) + " " + (rounded > 0 ? "+" : "") + rounded)
        }
        return parts.length > 0 ? parts.join(" · ")
                                : String((action || ({})).reason || "Bounded local adjustment")
    }

    function protectedDraftIntents() {
        if (!root.hasController)
            return []
        const report = root.agentEditController.lastRunReport || ({})
        return report.protectedDraftIntents || []
    }

    function isIntentProtected(action) {
        const boundarySerial = root.intentBoundarySerial
        const resolvedAction = action || ({})
        if (Boolean(resolvedAction.userProtected))
            return true
        const maskId = String(resolvedAction.maskId || "")
        const protectedIntents = root.protectedDraftIntents()
        for (let index = 0; index < protectedIntents.length; ++index) {
            if (String((protectedIntents[index] || ({})).maskId || "") === maskId)
                return true
        }
        return false
    }

    function setIntentProtected(action, protectedIntent) {
        if (!root.hasController)
            return false
        const finding = root.findingForLocalizedAction(action)
        const report = root.agentEditController.setDraftFindingProtected(
                           finding, protectedIntent) || ({})
        if (!Boolean(report.ok)) {
            const errors = report.errors || []
            root.correctionError = String(report.failureReason ||
                                          (errors.length > 0 ? errors[0]
                                                             : "That change could not be protected safely."))
            return false
        }
        if (protectedIntent && root.activeFindingMaskId === String(finding.maskId || "")) {
            root.correctionOpen = false
            root.correctionText = ""
        }
        root.intentBoundarySerial += 1
        root.correctionError = ""
        return true
    }

    function protectedIntentLabels() {
        const intent = root.draftEditIntent()
        const protectedLabels = []
        if (String(intent.globalChangePolicy || "") === "preserve")
            protectedLabels.push("Everything else")
        const fields = [
            { key: "crop", label: "Framing" },
            { key: "colorMode", label: "Color mode" },
            { key: "brightness", label: "Brightness" },
            { key: "palette", label: "Palette" },
            { key: "detail", label: "Detail" }
        ]
        for (let index = 0; index < fields.length; ++index) {
            const field = fields[index]
            if (String(intent[field.key] || "") === "preserve")
                protectedLabels.push(field.label)
        }
        const protectedIntents = root.protectedDraftIntents()
        for (let index = 0; index < protectedIntents.length; ++index) {
            const intent = protectedIntents[index] || ({})
            const label = root.findingTargetLabel(intent.target, intent.targetLabel)
            if (protectedLabels.indexOf(label) < 0)
                protectedLabels.push(label)
        }
        return protectedLabels
    }

    function openIntentCorrection(action) {
        const finding = root.findingForLocalizedAction(action)
        if (!root.requestFinding(finding)) {
            root.correctionError = "This local change no longer has an editable area."
            return false
        }
        root.correctionText = ""
        root.correctionOpen = true
        Qt.callLater(function() { correctionEditor.forceActiveFocus() })
        return true
    }

    function findingTargetLabel(target, targetLabel) {
        const explicitLabel = String(targetLabel || "").trim()
        const label = (explicitLabel.length > 0 ? explicitLabel
                                                : String(target || "image")).replace(/_/g, " ")
        return label.length > 0 ? label.charAt(0).toUpperCase() + label.slice(1) : "Image"
    }

    function requestFinding(finding) {
        const resolved = finding || ({})
        const maskId = String(resolved.maskId || "")
        if (maskId.length === 0)
            return false
        root.activeFindingMaskId = maskId
        root.activeFinding = resolved
        root.spatialTargetChosen = false
        root.spatialTargetPoint = ({})
        root.spatialTargetAssetId = ""
        root.correctionError = ""
        root.findingRequested(resolved)
        return true
    }

    function continuityReceiptText() {
        if (!root.hasController || !root.catalogModel)
            return ""
        const project = String(root.catalogModel.projectFilter || "all")
        if (project.length === 0 || project.toLowerCase() === "all")
            return ""
        const selectedStyleId = String(root.agentEditController.selectedStyleId || "")
        const styles = root.agentEditController.availableStyles || []
        let styleName = "Current style"
        for (let index = 0; index < styles.length; ++index) {
            const style = styles[index] || ({})
            if (String(style.id || "") === selectedStyleId) {
                styleName = String(style.name || styleName)
                break
            }
        }
        const history = root.agentEditController.runHistory || []
        const taste = root.agentEditController.approvedProjectTaste || ({})
        const approvalCount = Number(taste.approvalCount || 0)
        const correctionCount = Number(taste.correctionCount || 0)
        const rejectionCount = Number(taste.rejectionCount || 0)
        const visualReferentCount = Number(taste.visualReferentCount || 0)
        const calibration = taste.photographerCalibration || ({})
        const memoryParts = []
        if (approvalCount > 0)
            memoryParts.push(approvalCount + " approved " + (approvalCount === 1 ? "look" : "looks"))
        if (correctionCount > 0)
            memoryParts.push(correctionCount + " learned " + (correctionCount === 1 ? "correction" : "corrections"))
        if (rejectionCount > 0)
            memoryParts.push(rejectionCount + " avoided " + (rejectionCount === 1 ? "draft" : "drafts"))
        if (visualReferentCount > 0)
            memoryParts.push(visualReferentCount + " grounded " +
                             (visualReferentCount === 1 ? "object" : "objects"))
        if (Boolean(calibration.available))
            memoryParts.push(Number(calibration.caseCount || 0) + " blind ratings")
        const memory = memoryParts.length > 0
                ? memoryParts.join(" · ")
                : (history.length === 0
                   ? "new project memory"
                   : history.length + " recent " + (history.length === 1 ? "run" : "runs"))
        return "Project memory · " + project + " · " + styleName + " · " + memory
    }

    function projectTaste() {
        return root.hasController
                ? (root.agentEditController.approvedProjectTaste || ({}))
                : ({})
    }

    function tasteEvidence() {
        return root.projectTaste().evidence || []
    }

    function tasteNameText() {
        const name = String(root.projectTaste().name || "").trim()
        return name.length > 0 ? name : "Project Taste"
    }

    function catalogAssetForId(assetId) {
        const normalizedId = String(assetId || "").trim()
        if (!root.catalogModel || normalizedId.length === 0)
            return ({})
        if (typeof root.catalogModel.assetById === "function")
            return root.catalogModel.assetById(normalizedId) || ({})
        const count = Number(root.catalogModel.assetCount || 0)
        for (let index = 0; index < count; ++index) {
            const asset = root.catalogModel.assetAt(index) || ({})
            if (String(asset.assetId || "") === normalizedId)
                return asset
        }
        return ({})
    }

    function normalizedImageSource(source) {
        const value = String(source || "").trim()
        if (value.length === 0)
            return ""
        if (value.indexOf(":") >= 0)
            return value
        return value.charAt(0) === "/" ? "file://" + value : value
    }

    function tasteEvidenceThumbnail(evidence) {
        const ids = (evidence || ({})).assetIds || []
        for (let index = 0; index < ids.length; ++index) {
            const asset = root.catalogAssetForId(ids[index])
            const source = root.normalizedImageSource(asset.thumbnailSource || "")
            if (source.length > 0)
                return source
        }
        return ""
    }

    function tasteSummaryText() {
        const taste = root.projectTaste()
        const summary = String(taste.summary || "").trim()
        if (summary.length > 0)
            return summary
        if (root.tasteEvidence().length > 0)
            return "I'm carrying these confirmed choices into later edits in this project."
        return "I haven't learned this project's taste yet. I learn only from edits you explicitly approve."
    }

    function twoSpeedTastePreference() {
        return root.projectTaste().twoSpeedPreference || ({})
    }

    function twoSpeedTasteHeadline() {
        const preference = root.twoSpeedTastePreference()
        const preferred = String(preference.preferredVersion || "")
        if (!Boolean(preference.enoughEvidence))
            return "One approved choice. I’m watching, not adapting yet."
        if (preferred === "first-look")
            return "You tend to keep the restrained first look."
        if (preferred === "deep-review")
            return "You tend to keep evidence-backed deep refinement."
        return "Your proved-version choices are mixed."
    }

    function forgetTwoSpeedTastePreference() {
        if (!root.hasController ||
                typeof root.agentEditController.forgetTwoSpeedTastePreference !== "function")
            return
        const report = root.agentEditController.forgetTwoSpeedTastePreference(
                         root.twoSpeedTasteForgetArmed) || ({})
        if (Boolean(report.ok)) {
            root.twoSpeedTasteForgetArmed = false
            root.tasteActionMessage =
                    "First Look / Deep Review preference forgotten. Approved looks and existing edits stayed unchanged."
            return
        }
        if (Boolean(report.confirmationRequired)) {
            root.twoSpeedTasteForgetArmed = true
            root.tasteActionMessage =
                    "Confirm once more. This removes only the proved-version preference signal."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't forget that preference safely."))
    }

    function assignmentReflection() {
        return root.hasController
                ? (root.agentEditController.assignmentTasteReflection || ({}))
                : ({})
    }

    function assignmentReflectionAvailable() {
        const reflection = root.assignmentReflection()
        return Boolean(reflection.available) &&
                root.assignmentReflectionDismissedProjectKey !==
                String(reflection.projectKey || "")
    }

    function beginNorthStarFromAssignmentReflection() {
        const reflection = root.assignmentReflection()
        const proposal = String(reflection.proposedNorthStar || "").trim()
        if (!Boolean(reflection.available) || proposal.length === 0)
            return
        root.northStarDraftText = proposal
        root.northStarEditOpen = true
        root.northStarForgetArmed = false
        root.tasteActionMessage =
                "Reflection drafted for review. Nothing is remembered until you press Save North Star."
    }

    function dismissAssignmentReflection() {
        const reflection = root.assignmentReflection()
        root.assignmentReflectionDismissedProjectKey =
                String(reflection.projectKey || "")
        root.tasteActionMessage =
                "Reflection dismissed for this session. Project memory stayed unchanged."
    }

    function projectNorthStar() {
        return root.projectTaste().projectNorthStar || ({})
    }

    function projectNorthStarAvailable() {
        return String(root.projectNorthStar().brief || "").trim().length > 0
    }

    function projectNorthStarDraftReceipt() {
        if (!root.hasController || !root.draftReady)
            return ({})
        const report = root.agentEditController.lastRunReport || ({})
        return report.projectNorthStarReceipt || ({})
    }

    function projectNorthStarProofText() {
        const receipt = root.projectNorthStarDraftReceipt()
        if (Boolean(receipt.proofCritiqueConsidered))
            return "Local model + deterministic proof reviewed · final fit is your call"
        if (Boolean(receipt.proofValidated))
            return "Rendered proof passed deterministic checks · North Star fit is your call"
        return "Used in planning · North Star fit is your call"
    }

    function beginNorthStarEdit() {
        root.northStarDraftText = String(root.projectNorthStar().brief || "")
        root.northStarEditOpen = true
        root.northStarForgetArmed = false
        root.tasteActionMessage = ""
    }

    function saveNorthStar() {
        const brief = root.northStarDraftText.trim()
        if (!root.hasController ||
                typeof root.agentEditController.saveProjectNorthStar !== "function" ||
                brief.length < 12)
            return
        const report = root.agentEditController.saveProjectNorthStar(brief) || ({})
        if (Boolean(report.ok)) {
            root.northStarEditOpen = false
            root.northStarDraftText = ""
            root.northStarForgetArmed = false
            root.tasteActionMessage =
                    "North Star saved for future drafts. The current request, style, and safety rules still win."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't save this Project North Star."))
    }

    function forgetNorthStar() {
        if (!root.hasController ||
                typeof root.agentEditController.forgetProjectNorthStar !== "function")
            return
        const report = root.agentEditController.forgetProjectNorthStar(
                         root.northStarForgetArmed) || ({})
        if (Boolean(report.ok)) {
            root.northStarForgetArmed = false
            root.northStarEditOpen = false
            root.northStarDraftText = ""
            root.tasteActionMessage =
                    "North Star forgotten for future drafts. Existing edits and the active draft stayed unchanged."
            return
        }
        if (Boolean(report.confirmationRequired)) {
            root.northStarForgetArmed = true
            root.tasteActionMessage =
                    "Confirm once more. This forgets only the future project brief; existing edits stay unchanged."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't forget this Project North Star."))
    }

    function tasteMaturityLabel() {
        const maturity = String(root.projectTaste().maturity || "new")
        if (maturity === "calibrated")
            return "CALIBRATED"
        if (maturity === "established")
            return "ESTABLISHED"
        if (maturity === "learning")
            return "LEARNING"
        return "NEW"
    }

    function photographerCalibration() {
        return root.hasController
                ? (root.agentEditController.photographerCalibrationProfile || ({}))
                : ({})
    }

    function photographerCalibrationCoach() {
        return root.hasController
                ? (root.agentEditController.photographerCalibrationCoach || ({}))
                : ({})
    }

    function photographerTasteCheck() {
        if (!root.hasController ||
                typeof root.agentEditController.photographerTasteCheck !== "function")
            return ({})
        const calibration = root.agentEditController.photographerCalibrationProfile || ({})
        if (!Boolean(calibration.available))
            return ({})
        return root.agentEditController.photographerTasteCheck() || ({})
    }

    function startPhotographerTasteCheck() {
        if (!root.hasController ||
                typeof root.agentEditController.compareDraftWithPhotographerCalibration !== "function")
            return
        const result = root.agentEditController.compareDraftWithPhotographerCalibration() || ({})
        if (Boolean(result.accepted)) {
            root.tasteCheckMessage = "Rendering a real, more restrained direction…"
            root.reopenRequested()
            return
        }
        const errors = result.errors || []
        root.tasteCheckMessage = String(result.failureReason ||
                                        (errors.length > 0 ? errors[0]
                                                           : "The second direction could not be started."))
    }

    function importPhotographerCalibration(fileUrl) {
        if (!root.hasController ||
                typeof root.agentEditController.importPhotographerCalibrationProfileFile !== "function")
            return
        const report = root.agentEditController.importPhotographerCalibrationProfileFile(
                         String(fileUrl || "")) || ({})
        if (Boolean(report.ok)) {
            const profile = report.profile || ({})
            root.tasteActionMessage = "Photographer calibration active · " +
                    Number(profile.caseCount || 0) + " blind case" +
                    (Number(profile.caseCount || 0) === 1 ? "" : "s") +
                    " · future drafts stay reviewable."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't verify that calibration profile."))
    }

    function importPhotographerTastePassport(fileUrl) {
        if (!root.hasController ||
                typeof root.agentEditController.inspectPhotographerTastePassportFile !==
                "function")
            return
        const report = root.agentEditController.inspectPhotographerTastePassportFile(
                         String(fileUrl || "")) || ({})
        if (Boolean(report.ok)) {
            root.pendingTastePassportUrl = String(fileUrl || "")
            root.pendingTastePassportDigest = String(report.passportDigestSha256 || "")
            root.pendingTastePassportPreview = report
            root.tasteActionMessage = "Taste Passport verified · review its signed lessons before using them."
            return
        }
        root.pendingTastePassportUrl = ""
        root.pendingTastePassportDigest = ""
        root.pendingTastePassportPreview = ({})
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't verify that Taste Passport."))
    }

    function confirmPhotographerTastePassport() {
        if (!root.hasController || root.pendingTastePassportUrl.length === 0 ||
                root.pendingTastePassportDigest.length === 0 ||
                typeof root.agentEditController.importPhotographerTastePassportFile !==
                "function")
            return
        const report = root.agentEditController.importPhotographerTastePassportFile(
                         root.pendingTastePassportUrl,
                         root.pendingTastePassportDigest) || ({})
        if (Boolean(report.ok)) {
            const profile = report.profile || ({})
            root.tasteActionMessage = "Taste Passport active for this project · " +
                    Number(profile.caseCount || 0) + " blind case" +
                    (Number(profile.caseCount || 0) === 1 ? "" : "s") +
                    " · no photos or paths imported."
            root.pendingTastePassportUrl = ""
            root.pendingTastePassportDigest = ""
            root.pendingTastePassportPreview = ({})
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't activate that Taste Passport."))
    }

    function cancelPhotographerTastePassport() {
        root.pendingTastePassportUrl = ""
        root.pendingTastePassportDigest = ""
        root.pendingTastePassportPreview = ({})
        root.tasteActionMessage = "Taste Passport not imported · this project is unchanged."
    }

    function exportPhotographerTastePassport(fileUrl) {
        if (!root.hasController ||
                typeof root.agentEditController.exportPhotographerTastePassportFile !==
                "function")
            return
        const report = root.agentEditController.exportPhotographerTastePassportFile(
                         String(fileUrl || "")) || ({})
        if (Boolean(report.ok)) {
            root.tasteActionMessage = "Taste Passport saved · " +
                    Number(report.caseCount || 0) + " blind case" +
                    (Number(report.caseCount || 0) === 1 ? "" : "s") +
                    " · calibration only, no photos or paths."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't save that Taste Passport."))
    }

    function openPhotographerCalibrationReview(fileUrl) {
        if (!root.hasController ||
                typeof root.agentEditController.openPhotographerCalibrationReviewPacketFile !==
                "function")
            return
        const report = root.agentEditController.openPhotographerCalibrationReviewPacketFile(
                         String(fileUrl || ""), false) || ({})
        if (Boolean(report.ok) && Boolean(report.opened)) {
            root.activeCalibrationReviewPacketUrl = String(fileUrl || "")
            root.activeCalibrationReviewPacketDigest =
                    String(report.packetDigestSha256 || "")
            root.activeCalibrationReviewProjectKey =
                    String(root.projectTaste().projectKey || "")
            root.tasteActionMessage = Number(report.caseCount || 0) +
                    " blind proofs opened · your ratings stay human-only · " +
                    "copy the signed ratings, then choose Finish copied ratings here — " +
                    "Download JSON remains the recovery path."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't verify that review packet."))
    }

    function beginPhotographerCalibrationFinalize() {
        root.calibrationFinalizePending = false
        root.pendingCalibrationRatingsUrl = ""
        photographerCalibrationRatingsDialog.open()
    }

    function finishCopiedPhotographerCalibrationRatings() {
        if (!root.hasController ||
                typeof root.agentEditController.finalizePhotographerCalibrationReviewClipboard !==
                "function")
            return
        const activeProjectKey = String(root.projectTaste().projectKey || "")
        if (root.activeCalibrationReviewPacketUrl.length === 0 ||
                root.activeCalibrationReviewProjectKey.length === 0 ||
                root.activeCalibrationReviewProjectKey !== activeProjectKey) {
            root.tasteActionMessage =
                    "Open the blind review from this project before finishing copied ratings."
            return
        }
        const report = root.agentEditController
                .finalizePhotographerCalibrationReviewClipboard(
                    root.activeCalibrationReviewPacketUrl) || ({})
        if (Boolean(report.ok)) {
            root.activeCalibrationReviewPacketUrl = ""
            root.activeCalibrationReviewPacketDigest = ""
            root.activeCalibrationReviewProjectKey = ""
            root.tasteActionMessage = Number(report.caseCount || 0) +
                    " copied blind rating" +
                    (Number(report.caseCount || 0) === 1 ? "" : "s") +
                    " verified · calibration active · clipboard cleared · " +
                    "source manifest kept unchanged."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't verify the copied photographer ratings."))
    }

    function finishPhotographerCalibrationRatings(ratingsUrl) {
        const ratings = String(ratingsUrl || "")
        if (ratings.length === 0)
            return
        const activeProjectKey = String(root.projectTaste().projectKey || "")
        if (root.activeCalibrationReviewPacketUrl.length > 0 &&
                root.activeCalibrationReviewProjectKey.length > 0 &&
                root.activeCalibrationReviewProjectKey === activeProjectKey) {
            root.finalizePhotographerCalibration(
                        ratings, root.activeCalibrationReviewPacketUrl)
            return
        }
        root.activeCalibrationReviewPacketUrl = ""
        root.activeCalibrationReviewPacketDigest = ""
        root.activeCalibrationReviewProjectKey = ""
        root.pendingCalibrationRatingsUrl = ratings
        root.calibrationFinalizePending = true
        photographerCalibrationReviewDialog.open()
    }

    function finalizePhotographerCalibration(ratingsUrl, packetUrl) {
        if (!root.hasController ||
                typeof root.agentEditController.finalizePhotographerCalibrationReviewFiles !==
                "function")
            return
        const report = root.agentEditController.finalizePhotographerCalibrationReviewFiles(
                         String(ratingsUrl || ""), String(packetUrl || "")) || ({})
        root.calibrationFinalizePending = false
        root.pendingCalibrationRatingsUrl = ""
        if (Boolean(report.ok)) {
            root.activeCalibrationReviewPacketUrl = ""
            root.activeCalibrationReviewPacketDigest = ""
            root.activeCalibrationReviewProjectKey = ""
            root.tasteActionMessage = Number(report.caseCount || 0) +
                    " blind rating" +
                    (Number(report.caseCount || 0) === 1 ? "" : "s") +
                    " verified · calibration active for this project · " +
                    "source manifest kept unchanged."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't finish that photographer calibration."))
    }

    function saveTasteName() {
        const name = root.tasteRenameText.trim()
        if (!root.hasController || name.length === 0)
            return
        const report = root.agentEditController.renameProjectTaste(name) || ({})
        if (Boolean(report.ok)) {
            root.tasteRenameOpen = false
            root.tasteRenameText = ""
            root.tasteActionMessage = "Project look renamed. The learned evidence stayed unchanged."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't rename this project look safely."))
    }

    function requestTasteForget(evidence) {
        const item = evidence || ({})
        const kind = String(item.kind || "")
        const evidenceId = String(item.id || "")
        if (!root.hasController || kind.length === 0 || evidenceId.length === 0)
            return
        const confirming = root.tasteForgetPendingKind === kind &&
                root.tasteForgetPendingId === evidenceId
        const report = root.agentEditController.forgetProjectTasteEvidence(
                         kind, evidenceId, confirming) || ({})
        if (Boolean(report.ok)) {
            root.tasteForgetPendingKind = ""
            root.tasteForgetPendingId = ""
            root.tasteActionMessage = "Forgotten for future edits. Existing edits stayed unchanged."
            return
        }
        if (Boolean(report.confirmationRequired)) {
            root.tasteForgetPendingKind = kind
            root.tasteForgetPendingId = evidenceId
            root.tasteActionMessage = "Confirm once more. This only changes what I remember for this project."
            return
        }
        const errors = report.errors || []
        root.tasteActionMessage = String(report.failureReason ||
                                         (errors.length > 0 ? errors[0]
                                                            : "I couldn't update project memory safely."))
    }

    function assistantLightMode() {
        if (root.traveling)
            return "walk"
        if (root.awaitingInput)
            return "warning"
        if (root.draftReady)
            return "success"
        return String(root.activeEvent.characterMode || "processing")
    }

    function assistantLightTool() {
        if (root.traveling || root.awaitingInput || root.draftReady)
            return "none"
        const explicitTool = String(root.activeEvent.characterTool || "")
        if (explicitTool.length > 0)
            return explicitTool
        const mode = root.assistantLightMode()
        if (mode === "observe" || mode === "review")
            return "loupe"
        if (mode === "thinking")
            return "sampler"
        if (mode === "point")
            return "frame"
        if (mode === "adjust" || mode === "processing")
            return "tone-key"
        return "none"
    }

    Component.onCompleted: refreshActiveEvent()

    Timer {
        id: travelArrival
        interval: 470
        repeat: false
        onTriggered: {
            root.traveling = false
            root.finishArrival()
        }
    }

    Timer {
        id: speechTimer
        interval: Math.min(3600, 1200 + (root.headline().length + root.explanation().length) * 20)
        repeat: false
        onTriggered: root.speaking = false
    }

    Timer {
        id: localPlanningHeartbeat
        interval: 1000
        repeat: true
        running: root.localModelPlanningActive
        onTriggered: root.localPlanningElapsedSeconds += 1
    }

    onLocalModelPlanningActiveChanged: {
        if (root.localModelPlanningActive)
            root.localPlanningElapsedSeconds = 0
    }

    Connections {
        target: root.agentEditController
        function onRunEventsChanged() { root.refreshActiveEvent() }
        function onRunningChanged() { root.refreshActiveEvent() }
        function onDraftChanged() { root.refreshActiveEvent() }
        function onPendingAgentInputChanged() { root.refreshActiveEvent() }
        function onProgressivePreviewChanged() { root.refreshActiveEvent() }
        function onProjectTasteChanged() {
            const activeProjectKey = String(root.projectTaste().projectKey || "")
            if (root.activeCalibrationReviewPacketUrl.length > 0 &&
                    root.activeCalibrationReviewProjectKey !== activeProjectKey) {
                root.activeCalibrationReviewPacketUrl = ""
                root.activeCalibrationReviewPacketDigest = ""
                root.activeCalibrationReviewProjectKey = ""
            }
            root.tasteForgetPendingKind = ""
            root.tasteForgetPendingId = ""
        }
    }

    SequentialAnimation {
        id: speechArrival
        NumberAnimation {
            target: speechBubble
            property: "opacity"
            from: 0.35
            to: 1
            duration: root.reducedMotion ? 0 : 190
            easing.type: Easing.OutCubic
        }
    }

    SequentialAnimation {
        id: touchPulse
        loops: 2
        ParallelAnimation {
            NumberAnimation { target: touchIndicator; property: "scale"; from: 0.6; to: 1.5; duration: root.reducedMotion ? 0 : 320; easing.type: Easing.OutCubic }
            NumberAnimation { target: touchIndicator; property: "opacity"; from: 0.78; to: 0; duration: root.reducedMotion ? 0 : 320 }
        }
    }

    Item {
        id: agentStage
        width: 152
        height: 112
        x: Math.min(root.width - width - 16, root.agentTargetX)
        y: Math.min(root.height - height - 16, root.agentTargetY)

        Behavior on x {
            NumberAnimation { duration: root.reducedMotion ? 0 : 420; easing.type: Easing.InOutCubic }
        }
        Behavior on y {
            NumberAnimation { duration: root.reducedMotion ? 0 : 420; easing.type: Easing.InOutCubic }
        }

        Rectangle {
            id: touchIndicator
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 24
            height: 24
            radius: 12
            color: "transparent"
            border.width: 2
            border.color: theme.primaryAccent
            opacity: 0
        }

        LooksyAgentCharacter {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 132
            height: 102
            mode: root.assistantLightMode()
            tool: root.assistantLightTool()
            talking: root.speaking && !root.traveling
            celebrating: root.draftReady
            accentColor: theme.primaryAccent
        }

        Rectangle {
            width: 44
            height: 5
            radius: 3
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            color: theme.alpha(theme.brandInk, 0.28)
        }
    }

    Rectangle {
        id: speechBubble
        objectName: "agentWorkspaceSpeechBubble"
        width: Math.min(430, Math.max(300, root.width * 0.31))
        height: bubbleContent.implicitHeight + 34
        x: Math.max(18, Math.min(root.width - width - 18,
                                agentStage.x > root.width * 0.58
                                ? agentStage.x - width - 20
                                : agentStage.x + agentStage.width + 14))
        y: Math.max(18, Math.min(root.height - height - root.filmstripHeight - 12,
                                agentStage.y - height * 0.48))
        radius: 20
        color: theme.alpha(theme.brandPaper, 0.97)
        border.width: 0

        Behavior on x { NumberAnimation { duration: root.reducedMotion ? 0 : 360; easing.type: Easing.InOutCubic } }
        Behavior on y { NumberAnimation { duration: root.reducedMotion ? 0 : 360; easing.type: Easing.InOutCubic } }

        ColumnLayout {
            id: bubbleContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 17
            spacing: 7

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: root.assetLabel()
                    color: theme.alpha(theme.brandInk, 0.52)
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    font.letterSpacing: 1.0
                    font.weight: Font.DemiBold
                }
                Rectangle {
                    visible: root.running
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    radius: 4
                    color: theme.primaryAccent
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { from: 1; to: 0.32; duration: 520 }
                        NumberAnimation { from: 0.32; to: 1; duration: 520 }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: continuityReceipt.text.length > 0
                spacing: 7

                Label {
                    id: continuityReceipt
                    objectName: "agentWorkspaceContinuityReceipt"
                    Layout.fillWidth: true
                    text: root.continuityReceiptText()
                    color: theme.alpha(theme.primaryAccent, 0.82)
                    font.family: theme.fontMono
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    Accessible.name: "Agent project memory"
                    Accessible.description: text
                }

                LedgerButton {
                    objectName: "agentWorkspaceTasteMirrorButton"
                    text: root.tasteMirrorOpen ? "Close taste" : "Taste"
                    Accessible.name: root.tasteMirrorOpen
                                     ? "Close project taste mirror"
                                     : "Open project taste mirror"
                    onClicked: {
                        root.tasteMirrorOpen = !root.tasteMirrorOpen
                        root.tasteRenameOpen = false
                        root.tasteRenameText = ""
                        root.northStarEditOpen = false
                        root.northStarDraftText = ""
                        root.northStarForgetArmed = false
                        root.tasteForgetPendingKind = ""
                        root.tasteForgetPendingId = ""
                        root.twoSpeedTasteForgetArmed = false
                        root.tasteActionMessage = ""
                    }
                }
            }

            Rectangle {
                id: northStarDraftReceiptCard
                objectName: "agentWorkspaceNorthStarDraftReceipt"
                readonly property var receipt: root.projectNorthStarDraftReceipt()
                readonly property string northStarDraftReceiptContract:
                    "agent-project-north-star-draft-receipt-v1"
                Layout.fillWidth: true
                visible: Boolean(receipt.available)
                implicitHeight: northStarDraftReceiptColumn.implicitHeight + 18
                radius: 12
                color: theme.alpha(theme.primaryAccent, 0.075)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.28)

                ColumnLayout {
                    id: northStarDraftReceiptColumn
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "ASSIGNMENT COMPASS"
                            color: theme.primaryAccent
                            font.family: theme.fontMono
                            font.pixelSize: 8
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            objectName: "agentWorkspaceNorthStarDraftBoundary"
                            text: "USED IN PLAN · CURRENT REQUEST LED"
                            color: theme.alpha(theme.brandInk, 0.44)
                            font.family: theme.fontMono
                            font.pixelSize: 7
                            font.weight: Font.DemiBold
                        }
                    }

                    Label {
                        objectName: "agentWorkspaceNorthStarDraftBrief"
                        Layout.fillWidth: true
                        text: String(northStarDraftReceiptCard.receipt.brief || "")
                        color: theme.alpha(theme.brandInk, 0.76)
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        Accessible.name: "Project North Star used for this draft"
                        Accessible.description: text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7

                        Label {
                            objectName: "agentWorkspaceNorthStarDraftProof"
                            Layout.fillWidth: true
                            text: root.projectNorthStarProofText()
                            color: theme.alpha(theme.brandInk, 0.48)
                            font.family: theme.fontMono
                            font.pixelSize: 7
                            wrapMode: Text.WordWrap
                        }

                        Label {
                            text: "NO AUTO CULL · NO AUTO RETOUCH"
                            color: theme.alpha(theme.primaryAccent, 0.78)
                            font.family: theme.fontMono
                            font.pixelSize: 7
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            Rectangle {
                objectName: "agentWorkspaceTasteMirrorCard"
                readonly property string tasteMirrorContract: "agent-project-taste-mirror-v2"
                Layout.fillWidth: true
                visible: root.tasteMirrorOpen
                implicitHeight: tasteMirrorColumn.implicitHeight + 20
                radius: 13
                color: theme.alpha(theme.brandInk, 0.052)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.25)

                ColumnLayout {
                    id: tasteMirrorColumn
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 7

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            objectName: "agentWorkspaceTasteName"
                            Layout.fillWidth: true
                            text: root.tasteNameText()
                            color: theme.brandInk
                            font.family: theme.fontUi
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        LedgerButton {
                            objectName: "agentWorkspaceTasteRenameButton"
                            visible: Boolean(root.projectTaste().renamable)
                            text: root.tasteRenameOpen ? "Cancel" : "Rename"
                            Accessible.name: text + " project look"
                            onClicked: {
                                root.tasteRenameOpen = !root.tasteRenameOpen
                                root.tasteRenameText = root.tasteRenameOpen
                                        ? root.tasteNameText() : ""
                                root.tasteActionMessage = ""
                            }
                        }
                        Label {
                            text: root.tasteMaturityLabel() + " · THIS PROJECT ONLY"
                            color: theme.primaryAccent
                            font.family: theme.fontMono
                            font.pixelSize: 8
                            font.weight: Font.Bold
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.tasteRenameOpen
                        spacing: 7

                        TextField {
                            objectName: "agentWorkspaceTasteRenameEditor"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            text: root.tasteRenameText
                            placeholderText: "Name this project look"
                            selectByMouse: true
                            maximumLength: 80
                            color: theme.brandInk
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            onTextEdited: root.tasteRenameText = text
                            onAccepted: root.saveTasteName()
                            background: Rectangle {
                                radius: 8
                                color: theme.alpha(theme.brandPaper, 0.78)
                                border.width: 1
                                border.color: parent.activeFocus
                                              ? theme.primaryAccent
                                              : theme.alpha(theme.brandInk, 0.12)
                            }
                            Accessible.name: "Project look name"
                        }

                        BubbleButton {
                            objectName: "agentWorkspaceTasteRenameSaveButton"
                            text: "Save name"
                            primary: true
                            enabled: root.tasteRenameText.trim().length >= 2
                            onClicked: root.saveTasteName()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "What I'm learning from your approved choices"
                        color: theme.alpha(theme.brandInk, 0.48)
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                    }

                    Label {
                        objectName: "agentWorkspaceTasteSummary"
                        Layout.fillWidth: true
                        text: root.tasteSummaryText()
                        color: theme.alpha(theme.brandInk, 0.68)
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        lineHeight: 1.15
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        Accessible.name: "Current learned project taste"
                        Accessible.description: text
                    }

                    Rectangle {
                        id: twoSpeedTasteCard
                        objectName: "agentWorkspaceTwoSpeedTasteCard"
                        readonly property var preference:
                            root.twoSpeedTastePreference()
                        readonly property string twoSpeedTasteContract:
                            "agent-two-speed-taste-signal-v1"
                        Layout.fillWidth: true
                        visible: Boolean(preference.available)
                        implicitHeight: twoSpeedTasteColumn.implicitHeight + 18
                        radius: 10
                        color: theme.alpha(theme.primaryAccent, 0.075)
                        border.width: 1
                        border.color: theme.alpha(theme.primaryAccent, 0.28)

                        ColumnLayout {
                            id: twoSpeedTasteColumn
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "FIRST LOOK / DEEP REVIEW"
                                    color: theme.primaryAccent
                                    font.family: theme.fontMono
                                    font.pixelSize: 8
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.7
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    objectName: "agentWorkspaceTwoSpeedTasteAuthority"
                                    text: "CURRENT REQUEST WINS · NO AUTO SELECT"
                                    color: theme.alpha(theme.brandInk, 0.44)
                                    font.family: theme.fontMono
                                    font.pixelSize: 7
                                    font.weight: Font.Bold
                                }
                            }

                            Label {
                                objectName: "agentWorkspaceTwoSpeedTasteHeadline"
                                Layout.fillWidth: true
                                text: root.twoSpeedTasteHeadline()
                                color: theme.brandInk
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Label {
                                Layout.fillWidth: true
                                text: Number(twoSpeedTasteCard.preference.firstLookCount || 0) +
                                      " First Look · " +
                                      Number(twoSpeedTasteCard.preference.deepReviewCount || 0) +
                                      " Deep Review · learned after confirmed commits only"
                                color: theme.alpha(theme.brandInk, 0.54)
                                font.family: theme.fontMono
                                font.pixelSize: 8
                                wrapMode: Text.WordWrap
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    text: String(twoSpeedTasteCard.preference.planningGuidance || "")
                                    color: theme.alpha(theme.brandInk, 0.6)
                                    font.family: theme.fontUi
                                    font.pixelSize: 9
                                    lineHeight: 1.12
                                    maximumLineCount: 3
                                    wrapMode: Text.WordWrap
                                    elide: Text.ElideRight
                                    Accessible.name: "Proved-version planning guidance"
                                    Accessible.description: text
                                }

                                LedgerButton {
                                    objectName: "agentWorkspaceTwoSpeedTasteForgetButton"
                                    text: root.twoSpeedTasteForgetArmed
                                          ? "Confirm forget" : "Forget signal"
                                    Accessible.name: text + " for First Look and Deep Review preference"
                                    onClicked: root.forgetTwoSpeedTastePreference()
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: assignmentReflectionCard
                        objectName: "agentWorkspaceAssignmentReflectionCard"
                        readonly property var reflection:
                            root.assignmentReflection()
                        readonly property string reflectionContract:
                            "agent-assignment-taste-reflection-v1"
                        Layout.fillWidth: true
                        visible: root.assignmentReflectionAvailable()
                        implicitHeight: assignmentReflectionColumn.implicitHeight + 18
                        radius: 10
                        color: theme.alpha(theme.brandInk, 0.035)
                        border.width: 1
                        border.color: theme.alpha(theme.brandInk, 0.16)

                        ColumnLayout {
                            id: assignmentReflectionColumn
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "ASSIGNMENT REFLECTION"
                                    color: theme.primaryAccent
                                    font.family: theme.fontMono
                                    font.pixelSize: 8
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.7
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    objectName: "agentWorkspaceAssignmentReflectionBoundary"
                                    text: "NOTHING REMEMBERED YET"
                                    color: theme.alpha(theme.brandInk, 0.44)
                                    font.family: theme.fontMono
                                    font.pixelSize: 7
                                    font.weight: Font.Bold
                                }
                            }

                            Label {
                                objectName: "agentWorkspaceAssignmentReflectionHeadline"
                                Layout.fillWidth: true
                                text: String(assignmentReflectionCard.reflection.headline || "")
                                color: theme.brandInk
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                            }

                            Label {
                                objectName: "agentWorkspaceAssignmentReflectionEvidence"
                                Layout.fillWidth: true
                                text: {
                                    const observations = assignmentReflectionCard.reflection.observations || []
                                    if (observations.length === 0)
                                        return ""
                                    return String(observations[0].detail || "")
                                }
                                color: theme.alpha(theme.brandInk, 0.56)
                                font.family: theme.fontUi
                                font.pixelSize: 9
                                lineHeight: 1.12
                                maximumLineCount: 2
                                wrapMode: Text.WordWrap
                                elide: Text.ElideRight
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    text: "Review the wording first · current request still wins"
                                    color: theme.alpha(theme.brandInk, 0.42)
                                    font.family: theme.fontMono
                                    font.pixelSize: 7
                                    wrapMode: Text.WordWrap
                                }

                                LedgerButton {
                                    objectName: "agentWorkspaceAssignmentReflectionDismissButton"
                                    text: "Not now"
                                    Accessible.name: "Dismiss assignment reflection for this session"
                                    onClicked: root.dismissAssignmentReflection()
                                }

                                LedgerButton {
                                    objectName: "agentWorkspaceAssignmentReflectionShapeButton"
                                    text: "Shape North Star"
                                    Accessible.name: "Review assignment reflection as a Project North Star draft"
                                    onClicked: root.beginNorthStarFromAssignmentReflection()
                                }
                            }
                        }
                    }

                    Rectangle {
                        objectName: "agentWorkspaceNorthStarCard"
                        readonly property string northStarContract:
                            "agent-project-north-star-memory-v1"
                        Layout.fillWidth: true
                        implicitHeight: northStarColumn.implicitHeight + 18
                        radius: 10
                        color: theme.alpha(theme.primaryAccent, 0.07)
                        border.width: 1
                        border.color: theme.alpha(theme.primaryAccent, 0.24)

                        ColumnLayout {
                            id: northStarColumn
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "PROJECT NORTH STAR"
                                    color: theme.primaryAccent
                                    font.family: theme.fontMono
                                    font.pixelSize: 8
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.8
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    text: "FUTURE DRAFTS · CURRENT REQUEST WINS"
                                    color: theme.alpha(theme.brandInk, 0.42)
                                    font.family: theme.fontMono
                                    font.pixelSize: 7
                                    font.weight: Font.DemiBold
                                }
                            }

                            Label {
                                objectName: "agentWorkspaceNorthStarBrief"
                                Layout.fillWidth: true
                                visible: !root.northStarEditOpen
                                text: root.projectNorthStarAvailable()
                                      ? String(root.projectNorthStar().brief || "")
                                      : "Give this assignment one enduring promise—what every future draft should protect or feel like."
                                color: root.projectNorthStarAvailable()
                                       ? theme.alpha(theme.brandInk, 0.72)
                                       : theme.alpha(theme.brandInk, 0.44)
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                lineHeight: 1.16
                                wrapMode: Text.WordWrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                                Accessible.name: "Project North Star"
                                Accessible.description: text
                            }

                            TextArea {
                                objectName: "agentWorkspaceNorthStarEditor"
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.northStarEditOpen ? 62 : 0
                                visible: root.northStarEditOpen
                                text: root.northStarDraftText
                                placeholderText: "Keep this wedding intimate and documentary. Protect skin, highlights, and unposed moments."
                                wrapMode: TextEdit.Wrap
                                selectByMouse: true
                                color: theme.brandInk
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                onTextChanged: {
                                    if (text.length > 360) {
                                        text = text.slice(0, 360)
                                        return
                                    }
                                    root.northStarDraftText = text
                                }
                                background: Rectangle {
                                    radius: 8
                                    color: theme.alpha(theme.brandPaper, 0.82)
                                    border.width: 1
                                    border.color: parent.activeFocus
                                                  ? theme.primaryAccent
                                                  : theme.alpha(theme.brandInk, 0.12)
                                }
                                Accessible.name: "Edit Project North Star"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 7

                                Label {
                                    Layout.fillWidth: true
                                    text: root.northStarEditOpen
                                          ? root.northStarDraftText.trim().length + " / 360"
                                          : "Project-local qualitative guidance · no automatic recipe or culling authority"
                                    color: theme.alpha(theme.brandInk, 0.42)
                                    font.family: theme.fontMono
                                    font.pixelSize: 7
                                    wrapMode: Text.WordWrap
                                }

                                LedgerButton {
                                    objectName: "agentWorkspaceNorthStarCancelButton"
                                    visible: root.northStarEditOpen
                                    text: "Cancel"
                                    onClicked: {
                                        root.northStarEditOpen = false
                                        root.northStarDraftText = ""
                                        root.tasteActionMessage = ""
                                    }
                                }

                                BubbleButton {
                                    objectName: "agentWorkspaceNorthStarSaveButton"
                                    visible: root.northStarEditOpen
                                    text: "Save North Star"
                                    primary: true
                                    enabled: root.northStarDraftText.trim().length >= 12 &&
                                             root.northStarDraftText.trim().length <= 360
                                    onClicked: root.saveNorthStar()
                                }

                                LedgerButton {
                                    objectName: "agentWorkspaceNorthStarEditButton"
                                    visible: !root.northStarEditOpen
                                    text: root.projectNorthStarAvailable() ? "Edit" : "Add"
                                    onClicked: root.beginNorthStarEdit()
                                }

                                LedgerButton {
                                    objectName: "agentWorkspaceNorthStarForgetButton"
                                    visible: !root.northStarEditOpen &&
                                             root.projectNorthStarAvailable()
                                    text: root.northStarForgetArmed
                                          ? "Confirm forget" : "Forget"
                                    onClicked: root.forgetNorthStar()
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: calibrationReviewCard
                        objectName: "agentWorkspaceCalibrationReviewCard"
                        readonly property string calibrationReviewContract:
                            "agent-photographer-calibration-review-launch-v1"
                        readonly property string calibrationSessionHandoffContract:
                            "agent-photographer-calibration-session-handoff-v1"
                        readonly property var calibrationCoach:
                            root.photographerCalibrationCoach()
                        Layout.fillWidth: true
                        implicitHeight: calibrationReviewColumn.implicitHeight + 18
                        radius: 10
                        color: theme.alpha(theme.brandPaper, 0.66)
                        border.width: 1
                        border.color: theme.alpha(theme.brandInk, 0.09)

                        ColumnLayout {
                            id: calibrationReviewColumn
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "PHOTOGRAPHER CALIBRATION"
                                    color: theme.alpha(theme.brandInk, 0.5)
                                    font.family: theme.fontMono
                                    font.pixelSize: 8
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.7
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    text: "HUMAN-ONLY GATE"
                                    color: theme.primaryAccent
                                    font.family: theme.fontMono
                                    font.pixelSize: 7
                                    font.weight: Font.Bold
                                }
                            }

                            Label {
                                objectName: "agentWorkspaceTasteCalibrationStatus"
                                Layout.fillWidth: true
                                text: Boolean(root.photographerCalibration().available)
                                      ? String(root.photographerCalibration().summary ||
                                               "Blind photographer evidence is active.")
                                      : "Review the signed before/after proofs without seeing the machine verdict, then bring the resulting taste profile back to this project."
                                color: theme.alpha(theme.brandInk, 0.62)
                                font.family: theme.fontUi
                                font.pixelSize: 9
                                lineHeight: 1.12
                                maximumLineCount: 3
                                wrapMode: Text.WordWrap
                                elide: Text.ElideRight
                                Accessible.name: "Photographer calibration status"
                                Accessible.description: text
                            }

                            Rectangle {
                                id: calibrationCoachCard
                                objectName: "agentWorkspaceCalibrationCoachCard"
                                readonly property var lesson:
                                    calibrationReviewCard.calibrationCoach.primaryLesson || ({})
                                Layout.fillWidth: true
                                implicitHeight: visible
                                                ? calibrationCoachColumn.implicitHeight + 14 : 0
                                visible: Boolean(calibrationReviewCard.calibrationCoach.available)
                                radius: 8
                                color: theme.alpha(theme.primaryAccent, 0.075)
                                border.width: 1
                                border.color: theme.alpha(theme.primaryAccent, 0.24)

                                ColumnLayout {
                                    id: calibrationCoachColumn
                                    anchors.fill: parent
                                    anchors.margins: 7
                                    spacing: 3

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Label {
                                            objectName: "agentWorkspaceCalibrationCoachTitle"
                                            text: "CALIBRATION COACH"
                                            color: theme.primaryAccent
                                            font.family: theme.fontMono
                                            font.pixelSize: 7
                                            font.weight: Font.Bold
                                            font.letterSpacing: 0.6
                                        }

                                        Item { Layout.fillWidth: true }

                                        Label {
                                            text: "SIGNED LESSON · 1 OF " +
                                                  Number(calibrationReviewCard.calibrationCoach.lessonCount || 0)
                                            color: theme.alpha(theme.brandInk, 0.38)
                                            font.family: theme.fontMono
                                            font.pixelSize: 7
                                            font.weight: Font.DemiBold
                                        }
                                    }

                                    Label {
                                        objectName: "agentWorkspaceCalibrationCoachHeadline"
                                        Layout.fillWidth: true
                                        text: String(calibrationCoachCard.lesson.headline || "")
                                        color: theme.alpha(theme.brandInk, 0.78)
                                        font.family: theme.fontUi
                                        font.pixelSize: 9
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        objectName: "agentWorkspaceCalibrationCoachExplanation"
                                        Layout.fillWidth: true
                                        text: String(calibrationCoachCard.lesson.explanation || "")
                                        color: theme.alpha(theme.brandInk, 0.58)
                                        font.family: theme.fontUi
                                        font.pixelSize: 8
                                        maximumLineCount: 2
                                        wrapMode: Text.WordWrap
                                        elide: Text.ElideRight
                                        Accessible.name: "Calibration lesson"
                                        Accessible.description: text
                                    }

                                    Label {
                                        objectName: "agentWorkspaceCalibrationCoachBoundary"
                                        Layout.fillWidth: true
                                        text: String(calibrationCoachCard.lesson.decisionContrast || "") +
                                              " · NO AUTO TRAINING · NO THRESHOLD CHANGE"
                                        color: theme.alpha(theme.primaryAccent, 0.68)
                                        font.family: theme.fontMono
                                        font.pixelSize: 6
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.WordWrap
                                        Accessible.name: "Calibration Coach authority boundary"
                                        Accessible.description: text
                                    }
                                }
                            }

                            Rectangle {
                                id: tastePassportPreviewCard
                                objectName: "agentWorkspaceTastePassportPreviewCard"
                                readonly property var preview:
                                    root.pendingTastePassportPreview || ({})
                                readonly property var profile:
                                    preview.profile || ({})
                                Layout.fillWidth: true
                                implicitHeight: visible
                                                ? tastePassportPreviewColumn.implicitHeight + 14 : 0
                                visible: root.pendingTastePassportDigest.length > 0
                                radius: 8
                                color: theme.alpha(theme.primaryAccent, 0.085)
                                border.width: 1
                                border.color: theme.alpha(theme.primaryAccent, 0.34)

                                ColumnLayout {
                                    id: tastePassportPreviewColumn
                                    anchors.fill: parent
                                    anchors.margins: 7
                                    spacing: 4

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Label {
                                            text: "VERIFIED TASTE PASSPORT"
                                            color: theme.primaryAccent
                                            font.family: theme.fontMono
                                            font.pixelSize: 7
                                            font.weight: Font.Bold
                                            font.letterSpacing: 0.6
                                        }

                                        Item { Layout.fillWidth: true }

                                        Label {
                                            text: Number(tastePassportPreviewCard.preview.caseCount || 0) +
                                                  " BLIND CASE" +
                                                  (Number(tastePassportPreviewCard.preview.caseCount || 0) === 1
                                                   ? "" : "S")
                                            color: theme.alpha(theme.brandInk, 0.42)
                                            font.family: theme.fontMono
                                            font.pixelSize: 7
                                            font.weight: Font.DemiBold
                                        }
                                    }

                                    Label {
                                        objectName: "agentWorkspaceTastePassportPreviewSummary"
                                        Layout.fillWidth: true
                                        text: String(tastePassportPreviewCard.preview.summary ||
                                                     "Verified photographer calibration is ready to review.")
                                        color: theme.alpha(theme.brandInk, 0.7)
                                        font.family: theme.fontUi
                                        font.pixelSize: 9
                                        maximumLineCount: 2
                                        wrapMode: Text.WordWrap
                                        elide: Text.ElideRight
                                        Accessible.name: "Taste Passport lesson summary"
                                        Accessible.description: text
                                    }

                                    Label {
                                        objectName: "agentWorkspaceTastePassportPreviewBoundary"
                                        Layout.fillWidth: true
                                        text: Boolean(tastePassportPreviewCard.preview.wouldReplaceExisting)
                                              ? "REPLACES CURRENT CALIBRATION · THIS PROJECT ONLY · EDITS UNCHANGED"
                                              : "ADDS CALIBRATION · THIS PROJECT ONLY · EDITS UNCHANGED"
                                        color: theme.alpha(theme.primaryAccent, 0.76)
                                        font.family: theme.fontMono
                                        font.pixelSize: 7
                                        font.weight: Font.DemiBold
                                        wrapMode: Text.WordWrap
                                        Accessible.name: "Taste Passport import effect"
                                        Accessible.description: text
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 7

                                        Item { Layout.fillWidth: true }

                                        LedgerButton {
                                            objectName: "agentWorkspaceTastePassportCancelButton"
                                            text: "Not now"
                                            onClicked: root.cancelPhotographerTastePassport()
                                        }

                                        BubbleButton {
                                            objectName: "agentWorkspaceTastePassportConfirmButton"
                                            text: "Use for this project"
                                            primary: true
                                            Accessible.name: "Use verified Taste Passport for this project"
                                            Accessible.description:
                                                "Recheck the reviewed passport digest and apply only its calibration lessons to the active project"
                                            onClicked: root.confirmPhotographerTastePassport()
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 7

                                BubbleButton {
                                    objectName: "agentWorkspaceCalibrationReviewButton"
                                    text: "Review proofs"
                                    primary: !Boolean(root.photographerCalibration().available)
                                    Accessible.name: "Open a verified blind calibration review"
                                    Accessible.description:
                                        "The agent verifies the packet and proof hashes, then hands judgment to you"
                                    onClicked: photographerCalibrationReviewDialog.open()
                                }

                                BubbleButton {
                                    objectName: "agentWorkspaceCalibrationFinalizeButton"
                                    text: "Finish ratings"
                                    primary: false
                                    Accessible.name: "Finish exported photographer ratings"
                                    Accessible.description:
                                        "Verify the exported human ratings against the signed packet, create new local result files, and activate the project profile"
                                    onClicked: root.beginPhotographerCalibrationFinalize()
                                }

                                Item { Layout.fillWidth: true }
                            }

                            BubbleButton {
                                objectName: "agentWorkspaceCalibrationClipboardFinalizeButton"
                                Layout.fillWidth: true
                                visible: root.activeCalibrationReviewPacketUrl.length > 0
                                text: "Finish copied ratings"
                                primary: true
                                Accessible.name: "Finish copied photographer ratings"
                                Accessible.description:
                                    "Read ratings and notes from the clipboard, re-verify them against the exact signed packet, activate calibration, and clear the clipboard"
                                onClicked: root.finishCopiedPhotographerCalibrationRatings()
                            }

                            Label {
                                objectName: "agentWorkspaceCalibrationPacketReady"
                                Layout.fillWidth: true
                                visible: root.activeCalibrationReviewPacketUrl.length > 0
                                text: "VERIFIED PACKET READY · COPY FINISH IS ONE CLICK · JSON FALLBACK AVAILABLE · SESSION ONLY"
                                color: theme.alpha(theme.primaryAccent, 0.78)
                                font.family: theme.fontMono
                                font.pixelSize: 7
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                                Accessible.name: "Verified calibration packet ready for one-step finish"
                                Accessible.description:
                                    "The verified packet reference is kept only for this UI session; exported ratings are still re-verified before calibration is applied"
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 7

                                Label {
                                    Layout.fillWidth: true
                                    text: "TASTE PASSPORT · CALIBRATION ONLY · NO PHOTOS OR PATHS"
                                    color: theme.alpha(theme.primaryAccent, 0.72)
                                    font.family: theme.fontMono
                                    font.pixelSize: 7
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                    Accessible.name: "Portable photographer Taste Passport"
                                    Accessible.description:
                                        "Export or explicitly import only verified blind-review lessons for one project; photos, source paths, automatic training, and threshold changes stay excluded"
                                }

                                LedgerButton {
                                    objectName: "agentWorkspaceTastePassportExportButton"
                                    visible: Boolean(root.photographerCalibration().available)
                                    text: "Export"
                                    Accessible.name: "Export Taste Passport"
                                    Accessible.description:
                                        "Save this project's verified calibration lessons without photos, source paths, or project name"
                                    onClicked: photographerTastePassportExportDialog.open()
                                }

                                BubbleButton {
                                    objectName: "agentWorkspaceTastePassportImportButton"
                                    text: "Import"
                                    primary: false
                                    Accessible.name: "Import Taste Passport for this project"
                                    Accessible.description:
                                        "Verify and explicitly apply portable calibration lessons only to the active project"
                                    onClicked: photographerTastePassportImportDialog.open()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 7

                                Label {
                                    Layout.fillWidth: true
                                    text: Boolean(root.photographerCalibration().importedFromTastePassport)
                                          ? "Passport provenance verified · active project only"
                                          : "Advanced: import a signed calibration profile"
                                    color: theme.alpha(theme.brandInk, 0.42)
                                    font.family: theme.fontUi
                                    font.pixelSize: 8
                                }

                                LedgerButton {
                                    objectName: "agentWorkspaceTasteCalibrationImportButton"
                                    text: Boolean(root.photographerCalibration().available)
                                          ? "Replace profile" : "Import profile"
                                    Accessible.name: text + " for this project"
                                    onClicked: photographerCalibrationDialog.open()
                                }
                            }

                            Label {
                                objectName: "agentWorkspaceCalibrationReviewBoundary"
                                Layout.fillWidth: true
                                text: "PACKET VERIFIED BEFORE OPEN · VERIFIED PACKET KEPT FOR THIS SESSION · RATINGS VERIFIED BEFORE APPLY · MACHINE VERDICT HIDDEN · AGENT CANNOT RATE"
                                color: theme.alpha(theme.brandInk, 0.4)
                                font.family: theme.fontMono
                                font.pixelSize: 7
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                                Accessible.name: "Calibration review authority boundary"
                                Accessible.description: text
                            }
                        }
                    }

                    Repeater {
                        model: root.tasteEvidence().slice(
                                   0,
                                   Boolean(root.photographerCalibrationCoach().available)
                                   ? 3 : 4)

                        delegate: Rectangle {
                            id: tasteEvidenceDelegate
                            objectName: "agentWorkspaceTasteEvidenceRow"
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: tasteEvidenceRow.implicitHeight + 10
                            radius: 8
                            color: theme.alpha(theme.brandPaper, 0.64)
                            border.width: 1
                            border.color: theme.alpha(theme.brandInk, 0.07)

                            RowLayout {
                                id: tasteEvidenceRow
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 6
                                spacing: 7

                                Rectangle {
                                    Layout.preferredWidth: 52
                                    Layout.preferredHeight: 52
                                    radius: 7
                                    clip: true
                                    color: theme.alpha(theme.brandInk, 0.08)
                                    border.width: 1
                                    border.color: theme.alpha(theme.brandInk, 0.08)

                                    Image {
                                        objectName: "agentWorkspaceTasteEvidenceThumbnail"
                                        anchors.fill: parent
                                        source: root.tasteEvidenceThumbnail(
                                                    tasteEvidenceDelegate.modelData)
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        visible: String(source).length > 0
                                        Accessible.name: "Approved visual example"
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: root.tasteEvidenceThumbnail(
                                                     tasteEvidenceDelegate.modelData).length === 0
                                        text: String(tasteEvidenceDelegate.modelData.kind || "") ===
                                              "rejected-draft" ? "AVOID" : "TASTE"
                                        color: theme.alpha(theme.brandInk, 0.4)
                                        font.family: theme.fontMono
                                        font.pixelSize: 7
                                        font.weight: Font.Bold
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Label {
                                        Layout.fillWidth: true
                                        text: String(tasteEvidenceDelegate.modelData.title || "Learned choice")
                                        color: theme.brandInk
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: String(tasteEvidenceDelegate.modelData.summary || "")
                                        color: theme.alpha(theme.brandInk, 0.58)
                                        font.family: theme.fontUi
                                        font.pixelSize: 9
                                        maximumLineCount: 2
                                        wrapMode: Text.WordWrap
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: String(tasteEvidenceDelegate.modelData.detail || "")
                                        color: theme.alpha(theme.brandInk, 0.42)
                                        font.family: theme.fontMono
                                        font.pixelSize: 8
                                        elide: Text.ElideRight
                                    }
                                }

                                LedgerButton {
                                    objectName: "agentWorkspaceTasteForgetButton"
                                    text: root.tasteForgetPendingKind ===
                                          String(tasteEvidenceDelegate.modelData.kind || "") &&
                                          root.tasteForgetPendingId ===
                                          String(tasteEvidenceDelegate.modelData.id || "")
                                          ? "Confirm forget" : "Forget"
                                    Accessible.name: text + " " +
                                                     String(tasteEvidenceDelegate.modelData.title || "taste item")
                                    onClicked: root.requestTasteForget(
                                                   tasteEvidenceDelegate.modelData)
                                }
                            }
                        }
                    }

                    Label {
                        objectName: "agentWorkspaceTasteMoreCount"
                        Layout.fillWidth: true
                        visible: root.tasteEvidence().length > 4
                        text: "+ " + (root.tasteEvidence().length - 4) +
                              " older item" + (root.tasteEvidence().length - 4 === 1 ? "" : "s") +
                              " in the local memory vault"
                        color: theme.alpha(theme.brandInk, 0.44)
                        font.family: theme.fontMono
                        font.pixelSize: 8
                    }

                    Label {
                        objectName: "agentWorkspaceTasteActionMessage"
                        Layout.fillWidth: true
                        visible: root.tasteActionMessage.length > 0
                        text: root.tasteActionMessage
                        color: theme.primaryAccent
                        font.family: theme.fontUi
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        objectName: "agentWorkspaceTastePrivacy"
                        Layout.fillWidth: true
                        text: "LOCAL MEMORY · CALIBRATION IS SIGNED SUMMARY DATA · THUMBNAILS RESOLVE FROM CATALOG IDS · NO IMAGE BYTES · NO SOURCE PATHS · EDITS UNCHANGED WHEN FORGOTTEN"
                        color: theme.alpha(theme.brandInk, 0.42)
                        font.family: theme.fontMono
                        font.pixelSize: 7
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        Accessible.name: "Project taste memory privacy"
                        Accessible.description: text
                    }
                }
            }

            Label {
                objectName: "agentWorkspaceHeadline"
                Layout.fillWidth: true
                text: root.headline()
                color: theme.brandInk
                font.family: theme.fontDisplay
                font.pixelSize: 19
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }

            Label {
                objectName: "agentWorkspaceExplanation"
                Layout.fillWidth: true
                text: root.explanation()
                color: theme.alpha(theme.brandInk, 0.66)
                font.family: theme.fontUi
                font.pixelSize: 12
                lineHeight: 1.18
                wrapMode: Text.WordWrap
                maximumLineCount: 4
                elide: Text.ElideRight
            }

            Rectangle {
                objectName: "agentWorkspaceTwoSpeedDraftChoiceCard"
                readonly property string draftChoiceContract: "agent-two-speed-draft-choice-v1"
                Layout.fillWidth: true
                visible: root.twoSpeedDraftChoiceAvailable && !root.tasteMirrorOpen
                implicitHeight: twoSpeedDraftChoiceColumn.implicitHeight + 18
                radius: 12
                color: theme.alpha(theme.primaryAccent, 0.09)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.42)

                ColumnLayout {
                    id: twoSpeedDraftChoiceColumn
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "TWO PROVED VERSIONS"
                            color: theme.primaryAccent
                            font.family: theme.fontMono
                            font.pixelSize: 8
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            objectName: "agentWorkspaceTwoSpeedDraftChoiceReceipt"
                            text: "CURRENT · " +
                                  (root.selectedTwoSpeedDraftVersion === "first-look"
                                   ? "FIRST LOOK"
                                   : "DEEP REVIEW") +
                                  " · COMMIT SEPARATE"
                            color: theme.alpha(theme.brandInk, 0.52)
                            font.family: theme.fontMono
                            font.pixelSize: 7
                            font.weight: Font.Bold
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Deep review refined the first look. Keep either one."
                        color: theme.brandInk
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Both passed image proof. Discard still restores the exact pre-agent edit."
                        color: theme.alpha(theme.brandInk, 0.64)
                        font.family: theme.fontUi
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7

                        BubbleButton {
                            objectName: "agentWorkspaceFirstLookDraftButton"
                            Layout.fillWidth: true
                            text: "First Look"
                            primary: root.selectedTwoSpeedDraftVersion === "first-look"
                            Accessible.name: "Use the independently proved first look"
                            Accessible.description: "Show the fast proved version as the reversible uncommitted draft"
                            onClicked: root.agentEditController.chooseTwoSpeedDraftVersion("first-look")
                        }

                        BubbleButton {
                            objectName: "agentWorkspaceDeepReviewDraftButton"
                            Layout.fillWidth: true
                            text: "Deep Review"
                            primary: root.selectedTwoSpeedDraftVersion === "deep-review"
                            Accessible.name: "Use the proved deep review"
                            Accessible.description: "Show the deep reviewed version as the reversible uncommitted draft"
                            onClicked: root.agentEditController.chooseTwoSpeedDraftVersion("deep-review")
                        }
                    }
                }
            }

            Rectangle {
                objectName: "agentWorkspaceEditStrategyCard"
                readonly property string editStrategyContract: "agent-edit-strategy-route-v1"
                Layout.fillWidth: true
                visible: root.draftReady && !root.tasteMirrorOpen &&
                         String(root.editStrategy().primaryPath || "").length > 0
                implicitHeight: editStrategyColumn.implicitHeight + 18
                radius: 12
                color: theme.alpha(theme.brandInk, 0.052)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.24)

                ColumnLayout {
                    id: editStrategyColumn
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "EDIT APPROACH"
                            color: theme.alpha(theme.brandInk, 0.5)
                            font.family: theme.fontMono
                            font.pixelSize: 8
                            font.weight: Font.Bold
                            font.letterSpacing: 0.8
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            objectName: "agentWorkspaceEditStrategyAuthority"
                            text: "CHOSEN FOR THIS PHOTO"
                            color: theme.primaryAccent
                            font.family: theme.fontMono
                            font.pixelSize: 8
                            font.weight: Font.Bold
                        }
                    }

                    Label {
                        objectName: "agentWorkspaceEditStrategyLabel"
                        Layout.fillWidth: true
                        text: String(root.editStrategy().label || "")
                        color: theme.brandInk
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    Label {
                        objectName: "agentWorkspaceEditStrategyReason"
                        Layout.fillWidth: true
                        text: String(root.editStrategy().reason || "")
                        color: theme.alpha(theme.brandInk, 0.65)
                        font.family: theme.fontUi
                        font.pixelSize: 9
                        lineHeight: 1.12
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        Accessible.name: "Why the agent chose this edit strategy"
                        Accessible.description: text
                    }

                    Label {
                        objectName: "agentWorkspaceEditStrategyPreserved"
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: root.editStrategyPreservedText()
                        color: theme.alpha(theme.brandInk, 0.44)
                        font.family: theme.fontMono
                        font.pixelSize: 7
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Label {
                objectName: "agentWorkspaceTrustReceipt"
                Layout.fillWidth: true
                visible: root.draftReady && text.length > 0
                text: root.trustReceiptText()
                color: theme.alpha(theme.brandInk, 0.56)
                font.family: theme.fontMono
                font.pixelSize: 9
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                Accessible.name: "Edit check summary"
                Accessible.description: text
            }

            Rectangle {
                id: tasteCheckCard
                objectName: "agentWorkspacePhotographerTasteCheckCard"
                readonly property var tasteCheck: root.photographerTasteCheck()
                Layout.fillWidth: true
                visible: root.draftReady && !root.tasteMirrorOpen &&
                         Boolean(tasteCheck.recommended)
                implicitHeight: tasteCheckColumn.implicitHeight + 18
                radius: 12
                color: theme.alpha(theme.primaryAccent, 0.085)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.38)

                ColumnLayout {
                    id: tasteCheckColumn
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 5

                    Label {
                        objectName: "agentWorkspacePhotographerTasteCheckEyebrow"
                        text: "CALIBRATED TASTE CHECK"
                        color: theme.primaryAccent
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        font.weight: Font.Bold
                        font.letterSpacing: 0.7
                    }

                    Label {
                        objectName: "agentWorkspacePhotographerTasteCheckHeadline"
                        Layout.fillWidth: true
                        text: String(tasteCheckCard.tasteCheck.headline ||
                                     "I’m less certain about this direction.")
                        color: theme.brandInk
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        objectName: "agentWorkspacePhotographerTasteCheckExplanation"
                        Layout.fillWidth: true
                        text: String(tasteCheckCard.tasteCheck.explanation || "")
                        color: theme.alpha(theme.brandInk, 0.62)
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        lineHeight: 1.14
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7

                        LedgerButton {
                            objectName: "agentWorkspacePhotographerTasteCheckAction"
                            text: "Compare restrained version"
                            Accessible.name: "Render a calibrated alternative direction"
                            onClicked: root.startPhotographerTasteCheck()
                        }

                        Label {
                            objectName: "agentWorkspacePhotographerTasteCheckReceipt"
                            Layout.fillWidth: true
                            text: root.tasteCheckMessage.length > 0
                                  ? root.tasteCheckMessage
                                  : "2 REAL RENDERS · LEARNED ONLY AFTER CHOICE + COMMIT"
                            color: theme.alpha(theme.brandInk, 0.46)
                            font.family: theme.fontMono
                            font.pixelSize: 7
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Rectangle {
                id: pointResolutionCard
                objectName: "agentWorkspacePointResolutionCard"
                readonly property var pointResolution:
                    root.hasController
                    ? ((root.agentEditController.lastRunReport || ({})).pointGroundedCorrection || ({}))
                    : ({})
                Layout.fillWidth: true
                visible: root.draftReady && Boolean(pointResolution.applied)
                implicitHeight: pointResolutionColumn.implicitHeight + 16
                radius: 10
                color: theme.alpha(theme.primaryAccent, 0.075)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.34)

                ColumnLayout {
                    id: pointResolutionColumn
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 3

                    Label {
                        objectName: "agentWorkspacePointResolutionLabel"
                        Layout.fillWidth: true
                        text: String(pointResolutionCard.pointResolution.resolutionLabel ||
                                     "Feathered area fallback")
                        color: theme.primaryAccent
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        font.weight: Font.Bold
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        objectName: "agentWorkspacePointResolutionBoundary"
                        Layout.fillWidth: true
                        text: String(pointResolutionCard.pointResolution.resolutionMode || "") === "content-snap"
                              ? "CONTENT BOUNDARY FOUND · CLICK INCLUDED · OUTSIDE PROTECTED"
                              : "VISIBLE FEATHERED BOUNDARY · NO OBJECT CLAIM · OUTSIDE PROTECTED"
                        color: theme.alpha(theme.brandInk, 0.56)
                        font.family: theme.fontMono
                        font.pixelSize: 7
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Label {
                objectName: "agentWorkspaceProgressivePreviewReceipt"
                Layout.fillWidth: true
                visible: root.progressivePreviewVisible
                text: String(root.progressivePreview.mode || "") === "fast-planning-preview"
                      ? (root.keepValidatedFastAvailable
                         ? "FAST PROOF PASSED · USE NOW OR WAIT · COMMIT LOCKED"
                         : root.fastPreviewValidated
                           ? "FAST PROOF PASSED · " + root.fastDraftDeepTargetCountdownText() +
                             " · COMMIT LOCKED"
                           : "FAST PREVIEW · INDEPENDENT PROOF + DEEP REVIEW RUNNING · COMMIT LOCKED")
                      : "PREVIEW READY · FINAL CHECK RUNNING · COMMIT LOCKED"
                color: theme.primaryAccent
                font.family: theme.fontMono
                font.pixelSize: 9
                font.weight: Font.Bold
                wrapMode: Text.WordWrap
                Accessible.name: "Agent provisional preview status"
                Accessible.description: String(root.progressivePreview.mode || "") === "fast-planning-preview"
                                        ? (root.keepValidatedFastAvailable
                                           ? "The independently proved fast draft can now be kept as a reversible draft or left for deep review. Commit remains locked."
                                           : root.fastPreviewValidated
                                             ? "The fast draft passed independent proof. " +
                                               root.fastDraftDeepTargetCountdownText() +
                                               " while deep local vision continues. Commit remains locked."
                                             : "A reversible fast preview is visible while independent proof and deep local vision run. Commit remains locked.")
                                        : "The reversible edit is visible while validation runs. Apply remains locked."
            }

            Rectangle {
                objectName: "agentWorkspaceVisualFindingsCard"
                readonly property string intentLedgerContract: "agent-editable-intent-ledger-v1"
                Layout.fillWidth: true
                visible: root.draftReady && root.visualFindings().length > 0
                implicitHeight: visualFindingsColumn.implicitHeight + 20
                radius: 13
                color: theme.alpha(theme.primaryAccent, 0.07)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.24)

                ColumnLayout {
                    id: visualFindingsColumn
                    objectName: "agentWorkspaceIntentLedger"
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 7

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "Your edit"
                            color: theme.brandInk
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        Label {
                            visible: root.localizedActions().length > 0
                            text: root.localizedActions().length + " local " +
                                  (root.localizedActions().length === 1 ? "change" : "changes")
                            color: theme.primaryAccent
                            font.family: theme.fontMono
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                        }
                    }

                    Label {
                        text: "WHAT I SAW"
                        color: theme.alpha(theme.brandInk, 0.42)
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        font.letterSpacing: 0.8
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: root.visualFindings().slice(0, 2)
                        delegate: Rectangle {
                            id: findingDelegate
                            objectName: "agentWorkspaceFindingRow"
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: findingRow.implicitHeight + 8
                            radius: 8
                            color: root.activeFindingMaskId === String(modelData.maskId || "")
                                   ? theme.alpha(theme.primaryAccent, 0.12)
                                   : findingHitArea.containsMouse
                                     ? theme.alpha(theme.brandInk, 0.055)
                                     : "transparent"
                            Accessible.role: Accessible.Button
                            Accessible.name: String(modelData.observation || "Visual finding")
                            Accessible.description: String(modelData.maskId || "").length > 0
                                                    ? "Show this targeted adjustment on the photograph"
                                                    : "Observation only; no editable mask is attached"

                            RowLayout {
                                id: findingRow
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 4
                                spacing: 7

                                Rectangle {
                                    Layout.preferredWidth: 6
                                    Layout.preferredHeight: 6
                                    radius: 3
                                    color: String(findingDelegate.modelData.severity || "low") === "high"
                                           ? theme.primaryAccent
                                           : theme.alpha(theme.brandInk, 0.42)
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: String(findingDelegate.modelData.observation || "")
                                    color: theme.alpha(theme.brandInk, 0.72)
                                    font.family: theme.fontUi
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: root.findingTargetLabel(findingDelegate.modelData.target,
                                                                 findingDelegate.modelData.targetLabel) +
                                          (String(findingDelegate.modelData.maskId || "").length > 0
                                           ? (Boolean(findingDelegate.modelData.correctionApplied)
                                              ? " · Revised" : " · Show") : "")
                                    color: String(findingDelegate.modelData.maskId || "").length > 0
                                           ? theme.primaryAccent
                                           : theme.alpha(theme.brandInk, 0.46)
                                    font.family: theme.fontMono
                                    font.pixelSize: 8
                                    font.weight: Font.DemiBold
                                }
                            }

                            MouseArea {
                                id: findingHitArea
                                anchors.fill: parent
                                enabled: String(findingDelegate.modelData.maskId || "").length > 0
                                hoverEnabled: true
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: root.requestFinding(findingDelegate.modelData)
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: theme.alpha(theme.brandInk, 0.09)
                    }

                    Label {
                        visible: root.localizedActions().length > 0
                        text: "WHAT I'LL CHANGE"
                        color: theme.alpha(theme.brandInk, 0.42)
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        font.letterSpacing: 0.8
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        model: root.localizedActions().slice(0, 2)
                        delegate: Rectangle {
                            id: intentActionDelegate
                            objectName: "agentWorkspaceIntentAction"
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: intentActionRow.implicitHeight + 10
                            radius: 8
                            color: theme.alpha(theme.brandPaper, 0.68)
                            border.width: 1
                            border.color: theme.alpha(theme.brandInk, 0.08)

                            RowLayout {
                                id: intentActionRow
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 6
                                spacing: 7

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.findingTargetLabel(
                                                  intentActionDelegate.modelData.target,
                                                  intentActionDelegate.modelData.targetLabel)
                                        color: theme.brandInk
                                        font.family: theme.fontUi
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        objectName: "agentWorkspaceIntentActionSummary"
                                        Layout.fillWidth: true
                                        text: root.localizedActionSummary(intentActionDelegate.modelData)
                                        color: theme.alpha(theme.brandInk, 0.54)
                                        font.family: theme.fontMono
                                        font.pixelSize: 8
                                        elide: Text.ElideRight
                                    }
                                }

                                LedgerButton {
                                    text: "Show"
                                    Accessible.name: "Show this local change on the photo"
                                    onClicked: root.requestFinding(
                                                   root.findingForLocalizedAction(
                                                       intentActionDelegate.modelData))
                                }
                                LedgerButton {
                                    objectName: "agentWorkspaceIntentChangeButton"
                                    text: "Change"
                                    enabled: root.intentBoundarySerial >= 0 &&
                                             !root.isIntentProtected(intentActionDelegate.modelData)
                                    Accessible.name: "Change only this planned adjustment"
                                    Accessible.description: enabled
                                                            ? "Opens a correction scoped to this mask"
                                                            : "Unlock this protected change before revising it"
                                    onClicked: root.openIntentCorrection(
                                                   intentActionDelegate.modelData)
                                }
                                LedgerButton {
                                    objectName: "agentWorkspaceIntentProtectButton"
                                    readonly property bool intentProtected:
                                        root.intentBoundarySerial >= 0 &&
                                        root.isIntentProtected(intentActionDelegate.modelData)
                                    text: intentProtected ? "Kept" : "Keep"
                                    accent: intentProtected
                                    Accessible.name: intentProtected
                                                     ? "Unlock this protected planned adjustment"
                                                     : "Protect this planned adjustment"
                                    Accessible.description: intentProtected
                                                            ? "The agent cannot revise or rebuild this draft until you unlock it"
                                                            : "Keep this exact mask unchanged while you review the draft"
                                    onClicked: root.setIntentProtected(
                                                   intentActionDelegate.modelData,
                                                   !intentProtected)
                                }
                            }
                        }
                    }

                    Label {
                        id: intentProtectionLabel
                        objectName: "agentWorkspaceIntentProtection"
                        Layout.fillWidth: true
                        visible: root.protectedIntentLabels().length > 0
                        text: "PROTECTING · " + root.protectedIntentLabels().join(" · ")
                        color: theme.alpha(theme.brandInk, 0.48)
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        Accessible.name: "Protected parts of the edit"
                        Accessible.description: text
                    }
                }
            }

            Rectangle {
                objectName: "agentWorkspaceVisualCritiqueCard"
                Layout.fillWidth: true
                visible: root.runFailed &&
                         String(root.visualCritique().contract || "") ===
                         "agent-visual-critique-v1"
                implicitHeight: visualCritiqueColumn.implicitHeight + 22
                radius: 13
                color: theme.alpha(theme.primaryAccent, 0.065)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.28)

                ColumnLayout {
                    id: visualCritiqueColumn
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 5

                    Label {
                        objectName: "agentWorkspaceVisualCritiqueFinding"
                        Layout.fillWidth: true
                        text: String(root.visualCritique().finding ||
                                     "The proof found a visible cleanup problem.")
                        color: theme.brandInk
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        objectName: "agentWorkspaceVisualCritiqueEvidence"
                        Layout.fillWidth: true
                        text: String(root.visualCritique().evidenceSummary || "")
                        color: theme.alpha(theme.brandInk, 0.62)
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        objectName: "agentWorkspaceVisualCritiqueReceipt"
                        Layout.fillWidth: true
                        text: "ORIGINAL KEPT · OLD PATCH DISCARDED · NEW APPROVAL REQUIRED"
                        color: theme.primaryAccent
                        font.family: theme.fontMono
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        Accessible.name: "Proof rejection safety receipt"
                        Accessible.description: text
                    }
                }
            }

            Rectangle {
                objectName: "agentWorkspaceCorrectionCard"
                Layout.fillWidth: true
                visible: root.draftReady && root.correctionOpen
                implicitHeight: correctionColumn.implicitHeight + 22
                radius: 13
                color: theme.alpha(theme.brandInk, 0.055)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.32)

                ColumnLayout {
                    id: correctionColumn
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: root.activeFindingMaskId.length > 0 || root.spatialTargetChosen
                              ? "What should change here?"
                              : "What should I change?"
                        color: theme.brandInk
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    Label {
                        objectName: "agentWorkspaceCorrectionScope"
                        Layout.fillWidth: true
                        visible: root.activeFindingMaskId.length > 0 || root.spatialTargetChosen
                        text: root.spatialTargetChosen
                              ? "Target: content at this point · feathered fallback if unclear"
                              : "Changing only: " +
                                root.findingTargetLabel((root.activeFinding || ({})).target,
                                                        (root.activeFinding || ({})).targetLabel)
                        color: theme.primaryAccent
                        font.family: theme.fontMono
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    TextArea {
                        id: correctionEditor
                        objectName: "agentWorkspaceCorrectionEditor"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 66
                        text: root.correctionText
                        placeholderText: root.activeFindingMaskId.length > 0 ||
                                         root.spatialTargetChosen
                                         ? "Less on this, cooler, move left, or keep it unchanged…"
                                         : "Less warm, protect the skin, and keep the crop looser…"
                        wrapMode: TextEdit.WordWrap
                        color: theme.brandInk
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        onTextChanged: root.correctionText = text
                        background: Rectangle {
                            radius: 9
                            color: theme.alpha(theme.brandPaper, 0.82)
                            border.width: correctionEditor.activeFocus ? 1 : 0
                            border.color: theme.primaryAccent
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.correctionError.length > 0
                        text: root.correctionError
                        color: theme.primaryAccent
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        BubbleButton {
                            objectName: "agentWorkspaceCancelCorrectionButton"
                            text: "Cancel"
                            onClicked: {
                                if (root.spatialTargetChosen)
                                    root.cancelSpatialCorrection()
                                root.correctionOpen = false
                                root.correctionText = ""
                                root.correctionError = ""
                            }
                        }
                        BubbleButton {
                            objectName: "agentWorkspaceSubmitCorrectionButton"
                            text: "Make Changes"
                            primary: true
                            enabled: root.correctionText.trim().length > 0
                            onClicked: root.submitCorrection()
                        }
                    }
                }
            }

            Rectangle {
                objectName: "agentWorkspaceClarificationCard"
                Layout.fillWidth: true
                visible: root.awaitingClarification
                implicitHeight: clarificationColumn.implicitHeight + 22
                radius: 13
                color: theme.alpha(theme.primaryAccent, 0.07)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.28)

                ColumnLayout {
                    id: clarificationColumn
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 8

                    Label {
                        objectName: "agentWorkspaceClarificationPrompt"
                        Layout.fillWidth: true
                        text: root.pendingQuestionText()
                        color: theme.brandInk
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    TextArea {
                        id: pendingAnswerEditor
                        objectName: "agentWorkspaceClarificationEditor"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        text: root.pendingAnswerText
                        placeholderText: "Answer in your own words…"
                        wrapMode: TextEdit.WordWrap
                        color: theme.brandInk
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        onTextChanged: root.pendingAnswerText = text
                        background: Rectangle {
                            radius: 9
                            color: theme.alpha(theme.brandPaper, 0.84)
                            border.width: pendingAnswerEditor.activeFocus ? 1 : 0
                            border.color: theme.primaryAccent
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: root.pendingAnswerError.length > 0
                        text: root.pendingAnswerError
                        color: theme.primaryAccent
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        BubbleButton {
                            text: "Not now"
                            onClicked: root.agentEditController.dismissPendingAgentInput()
                        }
                        BubbleButton {
                            objectName: "agentWorkspaceClarificationContinue"
                            text: "Continue"
                            primary: true
                            enabled: root.pendingAnswerText.trim().length > 0
                            onClicked: root.submitPendingAnswer()
                        }
                    }
                }
            }

            Rectangle {
                objectName: "agentWorkspaceSelectionConfirmationCard"
                Layout.fillWidth: true
                visible: root.awaitingSelectionConfirmation
                implicitHeight: selectionConfirmationColumn.implicitHeight + 22
                radius: 13
                color: theme.alpha(theme.primaryAccent, 0.07)
                border.width: 1
                border.color: theme.alpha(theme.primaryAccent, 0.28)

                ColumnLayout {
                    id: selectionConfirmationColumn
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 7

                    Label {
                        objectName: "agentWorkspaceSelectionConfirmationPrompt"
                        Layout.fillWidth: true
                        text: "Which " + String(root.selectionConfirmation.targetLabel || "object") + "?"
                        color: theme.brandInk
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "The numbered outlines are candidates. Nothing is edited until you choose."
                        color: theme.alpha(theme.brandInk, 0.58)
                        font.family: theme.fontUi
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: root.selectionConfirmation.candidates || []

                        delegate: BubbleButton {
                            required property var modelData
                            required property int index
                            objectName: "agentWorkspaceSelectionCandidate" + String(index + 1)
                            Layout.fillWidth: true
                            primary: false
                            text: String(index + 1) + "  " +
                                  String(modelData.label || ("Candidate " + String(index + 1))) +
                                  "  ·  " + Math.round(Number(modelData.confidence || 0) * 100) + "%"
                            Accessible.name: "Choose " + String(modelData.label || ("candidate " + String(index + 1)))
                            onClicked: root.agentEditController.confirmGroundedSelectionCandidate(
                                           String(modelData.id || ""))
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: (root.running || root.draftReady || root.awaitingInput || root.runFailed) &&
                         !root.correctionOpen
                spacing: 8

                Item { Layout.fillWidth: true }

                BubbleButton {
                    visible: root.running && !root.awaitingInput
                    text: "Stop"
                    onClicked: root.agentEditController.cancelRun("user_cancelled")
                }
                BubbleButton {
                    objectName: "agentWorkspaceKeepValidatedFastDraftButton"
                    visible: root.keepValidatedFastAvailable
                    text: "Use Proved Draft"
                    primary: true
                    Accessible.name: "Keep independently proved fast draft"
                    Accessible.description: "Stop deep local planning and keep the visible fast result as a reversible, uncommitted draft"
                    onClicked: root.agentEditController.keepValidatedFastDraft()
                }
                BubbleButton {
                    objectName: "agentWorkspacePlanApprovalDismiss"
                    visible: root.awaitingPlanApproval
                    text: "Not now"
                    onClicked: root.agentEditController.dismissPendingAgentInput()
                }
                BubbleButton {
                    objectName: "agentWorkspacePlanApprovalApprove"
                    visible: root.awaitingPlanApproval
                    text: "Approve"
                    primary: true
                    onClicked: root.agentEditController.answerPendingUserInput(
                                   "I reviewed this request and approve continuing with a reversible draft.")
                }
                BubbleButton {
                    visible: root.awaitingSafeStop
                    text: "Dismiss"
                    onClicked: root.agentEditController.dismissPendingAgentInput()
                }
                BubbleButton {
                    objectName: "agentWorkspaceCancelRejectedRepairPoint"
                    visible: root.spatialTargetMode && root.rejectedRepairPointMode
                    text: "Cancel pointing"
                    onClicked: root.cancelSpatialCorrection()
                }
                BubbleButton {
                    objectName: "agentWorkspaceFailurePrimaryAction"
                    visible: root.runFailed && !root.spatialTargetMode
                    text: String(root.proofRejectionPresentation().primaryActionLabel ||
                                 "Try again")
                    primary: true
                    onClicked: {
                        if (String(root.proofRejectionPresentation().primaryActionKey || "") ===
                                "point-to-repair")
                            root.beginRejectedRepair()
                        else
                            root.reopenRequested()
                    }
                }
                BubbleButton {
                    visible: root.draftReady
                    text: "Discard"
                    onClicked: root.agentEditController.rollbackDraft("user_discarded")
                }
                BubbleButton {
                    objectName: "agentWorkspacePointToAreaButton"
                    visible: root.draftReady
                    text: root.spatialTargetMode ? "Cancel pointing" : "Point to area"
                    enabled: root.protectedIntentCount === 0
                    Accessible.name: root.spatialTargetMode
                                     ? "Cancel point grounded correction"
                                     : "Point to an exact area on the photo"
                    Accessible.description: enabled
                                            ? "Choose one image location, then describe a reversible local change"
                                            : "Unlock the protected draft intent before adding another correction"
                    onClicked: {
                        if (root.spatialTargetMode)
                            root.cancelSpatialCorrection()
                        else
                            root.beginSpatialCorrection()
                    }
                }
                BubbleButton {
                    objectName: "agentWorkspaceAskForChangesButton"
                    visible: root.draftReady
                    text: root.protectedIntentCount > 0
                          ? "Unlock kept change first"
                          : "Make Changes"
                    enabled: root.protectedIntentCount === 0
                    Accessible.description: enabled
                                            ? "Rebuild the reversible draft from your correction"
                                            : "A protected draft intent must be unlocked before replanning"
                    onClicked: {
                        root.correctionOpen = true
                        root.correctionError = ""
                        Qt.callLater(function() { correctionEditor.forceActiveFocus() })
                    }
                }
                BubbleButton {
                    visible: root.draftReady
                    text: "Commit Edits"
                    primary: true
                    onClicked: {
                        const report = root.agentEditController.commitDraft(true) || ({})
                        if (Boolean(report.ok))
                            root.draftCommitted(report)
                    }
                }
            }
        }
    }

    Rectangle {
        objectName: "agentWorkspaceLocalPlanningHeartbeat"
        visible: root.localModelPlanningActive
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.filmstripHeight +
                              (root.targetAssetIds.length > 1 ? 54 : 12)
        width: Math.min(parent.width - 32, localPlanningRow.implicitWidth + 30)
        height: 34
        radius: 17
        color: theme.alpha(theme.ink900, 0.92)
        border.width: 1
        border.color: theme.alpha(theme.primaryAccent, 0.42)

        Accessible.name: "Local model planning progress"
        Accessible.description: localPlanningLabel.text

        RowLayout {
            id: localPlanningRow
            anchors.centerIn: parent
            spacing: 9

            Rectangle {
                Layout.preferredWidth: 7
                Layout.preferredHeight: 7
                radius: 4
                color: theme.primaryAccent

                SequentialAnimation on opacity {
                    running: root.localModelPlanningActive && !root.reducedMotion
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 700; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutSine }
                }
            }

            Label {
                id: localPlanningLabel
                objectName: "agentWorkspaceLocalPlanningHeartbeatLabel"
                Layout.maximumWidth: Math.max(120, root.width - 78)
                text: root.localPlanningReceiptText()
                elide: Text.ElideRight
                color: theme.brandPaper
                font.family: theme.fontUi
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }
    }

    Rectangle {
        visible: root.running && root.targetAssetIds.length > 1
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.filmstripHeight + 12
        width: progressRow.implicitWidth + 30
        height: 34
        radius: 17
        color: theme.alpha(theme.ink900, 0.88)

        RowLayout {
            id: progressRow
            anchors.centerIn: parent
            spacing: 10
            Label {
                text: root.assetLabel()
                color: theme.brandPaper
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Rectangle {
                Layout.preferredWidth: 90
                Layout.preferredHeight: 3
                radius: 2
                color: theme.alpha(theme.brandPaper, 0.18)
                Rectangle {
                    width: parent.width * Math.max(0.05, (root.activeTargetIndex + 1) / Math.max(1, root.targetAssetIds.length))
                    height: parent.height
                    radius: 2
                    color: theme.primaryAccent
                    Behavior on width { NumberAnimation { duration: root.reducedMotion ? 0 : 260; easing.type: Easing.OutCubic } }
                }
            }
        }
    }
}
