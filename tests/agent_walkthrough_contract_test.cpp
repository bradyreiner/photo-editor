#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string read_file(const std::string& path) {
  std::ifstream stream(path);
  std::ostringstream content;
  content << stream.rdbuf();
  return content.str();
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool contains(const std::string& source, const std::string& value) {
  return source.find(value) != std::string::npos;
}

std::string section_between(
    const std::string& source,
    const std::string& begin,
    const std::string& end) {
  const auto begin_offset = source.find(begin);
  if (begin_offset == std::string::npos) {
    return {};
  }
  const auto end_offset = source.find(end, begin_offset + begin.size());
  if (end_offset == std::string::npos) {
    return {};
  }
  return source.substr(begin_offset, end_offset - begin_offset);
}

}  // namespace

int main() {
  const std::string root = PHOTO_EDITOR_SOURCE_DIR;
  const auto dialog = read_file(root + "/apps/desktop/qml/components/AgentWalkthroughDialog.qml");
  const auto conversation = read_file(root + "/apps/desktop/qml/components/AgentConversationView.qml");
  const auto session_terminal =
      read_file(root + "/apps/desktop/qml/components/AgentSessionTerminal.qml");
  const auto workspace_overlay = read_file(root + "/apps/desktop/qml/components/AgentWorkspaceOverlay.qml");
  const auto studio = read_file(root + "/apps/desktop/qml/workspaces/StudioWorkspace.qml");
  const auto studio_image_viewport =
      read_file(root + "/apps/desktop/qml/components/StudioImageViewport.qml");
  const auto shared_mask_overlay =
      read_file(root + "/apps/desktop/qml/workspaces/SharedMaskOverlay.qml");
  const auto controller_header = read_file(root + "/apps/desktop/src/agent_edit_controller.h");
  const auto controller_source = read_file(root + "/apps/desktop/src/agent_edit_controller.cpp");
  const auto local_model_source = read_file(root + "/apps/desktop/src/agent_local_model.cpp");
  const auto agent_light = read_file(root + "/apps/desktop/qml/components/LooksyLight.qml");
  const auto looksy_mark = read_file(root + "/apps/desktop/qml/components/LooksyMark.qml");
  const auto looksy_character = read_file(root + "/apps/desktop/qml/components/LooksyAgentCharacter.qml");
  const auto assistant_dock = read_file(root + "/apps/desktop/qml/components/LooksyAssistantDock.qml");
  const auto progress_strip = read_file(root + "/apps/desktop/qml/components/ProgressStrip.qml");
  const auto main_qml = read_file(root + "/apps/desktop/qml/Main.qml");

  const auto visual_search = read_file(root + "/apps/desktop/qml/components/VisualSearchPanel.qml");
  if (!contains(studio, "function openAgentRunDialog(explicitScope)") ||
      !contains(studio, "? explicitScope : root.bestAgentScopeMode(root.agentScopeMode)") ||
      !contains(studio, "root.openAgentRunDialog(catalogAssets.selectedAssetIds.length > 0 ? \"selected-assets\" : \"current-photo\")") ||
      !contains(visual_search, "signal editRequested()") ||
      contains(visual_search, "commitDraft(") || contains(visual_search, "applyEditForScope(")) {
    return fail("visual search must hand explicit chosen targets to review without applying or broadening scope");
  }

  if (!contains(dialog, "property var sessionController: null") ||
      !contains(dialog, "property var sessionContext: ({})") ||
      !contains(dialog, "if (!root.hasSessionController() && root.visible && root.agentRunning())") ||
      !contains(conversation, "AgentSessionTerminal {") ||
      !contains(conversation, "visible: root.sessionFlowAvailable") ||
      !contains(session_terminal, "sessionController.submit(prompt, root.sessionContext())") ||
      !contains(session_terminal, "root.sessionController.answerQuestions(visibleAnswers)") ||
      !contains(session_terminal, "root.sessionController.reviseAnswers(answers)") ||
      !contains(session_terminal, "root.sessionController.start()") ||
      !contains(session_terminal, "root.dialog.agentEditController.cancelRun(\"user_requested\")") ||
      !contains(session_terminal, "root.dialog.agentEditController.revertTask()") ||
      !contains(session_terminal, "objectName: \"agentSessionStartButton\"") ||
      !contains(session_terminal, "objectName: \"agentSessionStopButton\"") ||
      !contains(session_terminal, "objectName: \"agentSessionRevertButton\"") ||
      !contains(session_terminal, "objectName: \"agentSessionReviseButton\"") ||
      !contains(session_terminal, "function suggestedCopyNeedsReview()") ||
      !contains(session_terminal, "Boolean(root.plan.copySuggestionApprovalRequired)") ||
      !contains(session_terminal, "SUGGESTED COPY · REVIEW BEFORE START") ||
      contains(session_terminal, "ScrollView {")) {
    return fail("the injected session must use the conversational plan/Start flow, real Stop/Revert actions, and a non-scrolling terminal");
  }

  if (!contains(dialog, "ai-edit:v13|entry:left-rail-agent-terminal") ||
      !contains(dialog, "style-start:three-path-project-sticky") ||
      !contains(dialog, "sequence:direction-inspiration-live-work-review") ||
      !contains(dialog, "surface:crt-with-character") ||
      contains(dialog, "compact-crt-bezel-v1.png") ||
      contains(dialog, "BrandWordmark {") ||
      contains(conversation, "LooksyAgentCharacter {") ||
      !contains(dialog, "property real presentationProgress: 0") ||
      !contains(dialog, "scale: 0.965 + root.presentationProgress * 0.035") ||
      !contains(dialog, "inspiration:visual-selection") ||
      !contains(dialog, "function open()") ||
      !contains(dialog, "Accessible.role: Accessible.Dialog") ||
      !contains(dialog, "function focusCurrentPageEntry()") ||
      !contains(dialog, "id: focusRestoreTimer") ||
      !contains(dialog, "objectName: \"agentWalkthroughEscapeShortcut\"") ||
      !contains(dialog, "objectName: \"agentReturnToEditorButton\"") ||
      !contains(conversation, "function focusPageEntry()") ||
      !contains(conversation, "objectName: \"agentChangePromptButton\"") ||
      !contains(conversation, "Keys.priority: Keys.BeforeItem") ||
      !contains(conversation, "objectName: \"agentLivePreviewImage\"") ||
      !contains(conversation, "objectName: \"agentLiveActivityTimeline\"") ||
      !contains(conversation, "objectName: \"aiEditPlanVerificationSummary\"") ||
      !contains(conversation, "objectName: \"agentRunGoalLabel\"") ||
      !contains(dialog, "function preparePromptSuggestions()") ||
      !contains(dialog, "suggestInspirationForPrompt(prompt)") ||
      !contains(dialog, "scope-override:details") ||
      !contains(dialog, "inspiration:visual-selection") ||
      !contains(dialog, "function availableScopeTiles()") ||
      !contains(dialog, "function filteredScopeActive()") ||
      !contains(dialog, "scope === \"filtered-library\"") ||
      !contains(dialog, "key: \"filtered-library\"") ||
      !contains(dialog, "Only the photos visible in the current filter.") ||
      !contains(dialog, "function chooseAvailableScope(index)") ||
      !contains(dialog, "function scopeRiskMessage()") ||
      !contains(dialog, "function styleReferencePreflight()") ||
      !contains(dialog, "function styleReferenceStatusText()") ||
      !contains(dialog, "root.inspirationAssetIds = root.selectedInspirationPhotoIds()") ||
      !contains(dialog, "function correctionTransferPreview()") ||
      !contains(dialog, "function correctionTransferAvailable()") ||
      !contains(dialog, "function correctionTransferHeadline()") ||
      !contains(dialog, "function correctionTransferLesson()") ||
      !contains(dialog, "function correctionTransferDetail()") ||
      !contains(dialog, "function assignmentStoryMap()") ||
      !contains(dialog, "function assignmentStoryMapAvailable()") ||
      !contains(dialog, "function approvedAssignmentStoryReceipt()") ||
      !contains(dialog, "function approvedStoryDirectionAvailable()") ||
      !contains(dialog, "function assignmentStoryEyebrow()") ||
      !contains(dialog, "function assignmentStoryReceiptText()") ||
      !contains(dialog, "function storyPreferenceStatus()") ||
      !contains(dialog, "function storyPreferenceRemembered()") ||
      !contains(dialog, "function canManageStoryPreference()") ||
      !contains(dialog, "function manageStoryPreference()") ||
      !contains(dialog, "function assignmentStoryArcText()") ||
      !contains(dialog, "function displayedSample()") ||
      !contains(dialog, "function canShowPreviousSample()") ||
      !contains(dialog, "function canShowNextSample()") ||
      !contains(dialog, "function showPreviousSample()") ||
      !contains(dialog, "function showNextSample()") ||
      !contains(dialog, "function shownStoryAssignment()") ||
      !contains(dialog, "function shownStoryRoleEvidence()") ||
      !contains(dialog, "function shownStoryRoleEvidenceLabel()") ||
      !contains(dialog, "function shownStoryRoleReason()") ||
      !contains(dialog, "function assignmentStoryRoleOptions()") ||
      !contains(dialog, "function canDirectShownStoryRole()") ||
      !contains(dialog, "function canAssignShownStoryRole(role)") ||
      !contains(dialog, "function assignShownStoryRole(role)") ||
      !contains(dialog, "function canPromoteShownStoryHero()") ||
      !contains(dialog, "function promoteShownStoryHero()") ||
      !contains(dialog, "function proofRejectionPresentation()") ||
      !contains(dialog, "Adjust the target or direction") ||
      !contains(dialog, "The rest of the batch stays untouched until you approve") ||
      !contains(dialog, "function presentAppliedResult(report)") ||
      !contains(dialog, "AgentTerminalScreen {")) {
    return fail("agent entry should use the resolved workspace target before prompt and inspiration, then apply the result");
  }

  if (!contains(conversation, "What’s your goal for this photo?") ||
      !contains(conversation, "I have the photos from your workspace") ||
      !contains(conversation, "I have the photo from your workspace") ||
      !contains(conversation, "objectName: \"agentPromptEditor\"") ||
      !contains(conversation, "objectName: \"agentStyleStartCard\"") ||
      !contains(conversation, "objectName: \"agentCurrentProjectLookLabel\"") ||
      !contains(conversation, "objectName: \"agentEditingFreedomCard\"") ||
      !contains(conversation, "agent-editing-freedom-v1|modes:protect-balanced-transform") ||
      !contains(conversation, "objectName: \"agentEditingFreedomProtect\"") ||
      !contains(conversation, "objectName: \"agentEditingFreedomBalanced\"") ||
      !contains(conversation, "objectName: \"agentEditingFreedomTransform\"") ||
      !contains(dialog, "Bolder color, crop, and local work") ||
      !contains(conversation, "objectName: \"agentLearnLookFromEditsButton\"") ||
      !contains(conversation, "objectName: \"agentDescribeLookButton\"") ||
      !contains(conversation, "objectName: \"agentBuiltInLooksButton\"") ||
      !contains(conversation, "objectName: \"agentStyleReferencePreflight\"") ||
      !contains(conversation, "objectName: \"agentPrimaryCreateDraftButton\"") ||
      !contains(conversation, "objectName: \"agentDetailsToggle\"") ||
      !contains(conversation, "objectName: \"agentDetailsPanel\"") ||
      !contains(conversation, "objectName: \"agentAddInspirationButton\"") ||
      !contains(conversation, "objectName: \"agentLiveMcpStatusLabel\"") ||
      !contains(conversation, "objectName: \"agentDetailsRetouchRuntimeLabel\"") ||
      !contains(dialog, "function retouchRuntimeLabel()") ||
      !contains(dialog, "failed proofs stay unchanged") ||
      !contains(conversation, "objectName: \"agentLiveMemoryVaultLabel\"") ||
      !contains(conversation, "objectName: \"agentLiveRunAuditPanel\"") ||
      !contains(conversation, "objectName: \"agentOpenMemoryVaultInObsidianButton\"") ||
      !contains(conversation, "objectName: \"agentExportRedactedMemoryButton\"") ||
      !contains(dialog, "function refreshDetailsSnapshot()") ||
      !contains(dialog, "function latestToolCallSummary()") ||
      !contains(dialog, "function exportRedactedMemoryVault()") ||
      !contains(dialog, "function showEditedLooks()") ||
      !contains(dialog, "function saveWordsAsLook()") ||
      !contains(dialog, "function showBuiltInLooks()") ||
      !contains(dialog, "function batchForecastLabel()") ||
      !contains(dialog, "function batchForecastExplanation()") ||
      !contains(dialog, "function editingFreedomMode()") ||
      !contains(dialog, "function setEditingFreedomMode(mode)") ||
      !contains(conversation, "visible: root.dialog.detailsExpanded") ||
      !contains(conversation, "Add inspiration") ||
      !contains(conversation, "These could help shape the edit") ||
      !contains(conversation, "OPTIONAL PHOTO REFERENCES") ||
      !contains(conversation, "objectName: \"agentScopeSelector\"") ||
      !contains(conversation, "objectName: \"agentTargetSummary\"") ||
      !contains(conversation, "root.dialog.chooseAvailableScope(index)") ||
      !contains(conversation, "objectName: \"agentScopeRiskMessage\"") ||
      !contains(conversation, "Start editing ") ||
      !contains(conversation, "Inspiration is never edited") ||
      !contains(conversation, "objectName: \"aiEditAskForChangesButton\"") ||
      !contains(conversation, "objectName: \"aiEditSampleRevisionPanel\"") ||
      !contains(conversation, "objectName: \"aiEditSampleFeedbackEditor\"") ||
      !contains(conversation, "objectName: \"aiEditSubmitSampleRevisionButton\"") ||
      !contains(conversation, "objectName: \"aiEditCorrectionTransferPreview\"") ||
      !contains(conversation, "objectName: \"aiEditCorrectionTransferHeadline\"") ||
      !contains(conversation, "objectName: \"aiEditCorrectionTransferLesson\"") ||
      !contains(conversation, "objectName: \"aiEditCorrectionTransferDetail\"") ||
      !contains(conversation, "objectName: \"aiEditAssignmentStoryMap\"") ||
      !contains(conversation, "objectName: \"aiEditAssignmentStoryArc\"") ||
      !contains(conversation, "objectName: \"aiEditAssignmentStoryReceipt\"") ||
      !contains(conversation, "objectName: \"aiEditApprovedStoryDeliveryReceipt\"") ||
      !contains(conversation, "objectName: \"aiEditApprovedStoryEyebrow\"") ||
      !contains(conversation, "objectName: \"aiEditApprovedStoryHeadline\"") ||
      !contains(conversation, "objectName: \"aiEditApprovedStoryBoundary\"") ||
      !contains(conversation, "objectName: \"aiEditStoryPreferenceAction\"") ||
      !contains(conversation, "objectName: \"aiEditStoryPreferenceActionMessage\"") ||
      !contains(conversation, "objectName: \"aiEditStoryRoleEvidence\"") ||
      !contains(conversation, "objectName: \"aiEditStoryRoleEvidenceTitle\"") ||
      !contains(conversation, "objectName: \"aiEditStoryRoleEvidenceReason\"") ||
      !contains(conversation, "Layout.minimumHeight: visible ? implicitHeight : 0") ||
      !contains(conversation, "Layout.minimumHeight: root.dialog.assignmentStoryMapAvailable() ? 480 : 220") ||
      !contains(conversation, "NO SCENE CLAIM") ||
      !contains(conversation, "objectName: \"aiEditStorySampleNavigator\"") ||
      !contains(conversation, "objectName: \"aiEditStoryPreviousSample\"") ||
      !contains(conversation, "objectName: \"aiEditStorySamplePosition\"") ||
      !contains(conversation, "objectName: \"aiEditStoryNextSample\"") ||
      !contains(conversation, "objectName: \"aiEditAssignmentStoryRoleButton\"") ||
      !contains(conversation, "objectName: \"aiEditReviewBeforeImage\"") ||
      !contains(conversation, "objectName: \"aiEditReviewAfterImage\"") ||
      !contains(conversation, "objectName: \"aiEditAssignmentStoryRolePopupSurface\"") ||
      !contains(conversation, "aiEditStoryRole") ||
      !contains(conversation, "Accessible.role: Accessible.RadioButton") ||
      !contains(conversation, "Accessible.name: String(modelData.label || roleValue)") ||
      !contains(conversation, "Accessible.description: String(modelData.detail || \"\") +") ||
      !contains(conversation, "Accessible.checked: currentRole") ||
      !contains(conversation, "Shown · ") ||
      !contains(dialog, "PHOTOGRAPHER STEERED · REAL PROOFS REBUILT") ||
      !contains(dialog, "STORY MAP · REVIEW COVERAGE") ||
      !contains(dialog, "STORY DIRECTION · APPROVED FOR DELIVERY") ||
      !contains(dialog, "NO ROLE-BASED RECIPE CHANGES") ||
      !contains(dialog, "REMEMBERED FOR THIS PROJECT") ||
      !contains(dialog, "FUTURE STORY SUGGESTIONS ONLY") ||
      !contains(dialog, "NO AUTO-CULL · SOURCE ORDER KEPT") ||
      !contains(conversation, "objectName: \"aiEditConsistencyMap\"") ||
      !contains(conversation, "agent-professional-consistency-map-v1|basis:within-lighting-group-relative-render-response") ||
      !contains(conversation, "objectName: \"aiEditConsistencyHeadline\"") ||
      !contains(conversation, "objectName: \"aiEditConsistencyGroupList\"") ||
      !contains(conversation, "objectName: consistencyGroupCard.index === 0") ||
      !contains(dialog, "function consistencyMap()") ||
      !contains(dialog, "function consistencyGroups()") ||
      !contains(conversation, "MEMORY AFTER APPLY") ||
      !contains(conversation, "text: \"Make Changes\"") ||
      !contains(dialog, "? \"Create Samples\" : \"Create Edit\"") ||
      !contains(conversation, "? \"Discard samples\" : \"Discard edit\"") ||
      !contains(conversation, "? \"Apply to All\"") ||
      !contains(conversation, ": \"Commit Edits\"") ||
      !contains(conversation, "root.dialog.editCommitted") ||
      !contains(conversation, "? \"Done\"") ||
      !contains(conversation, "root.dialog.close()") ||
      !contains(dialog, "may need your eye") ||
      contains(dialog, "return \"Preflight · \"") ||
      !contains(dialog, "root.agentEditController.reviseSampleReview(feedback)") ||
      !contains(conversation, "objectName: \"aiEditCompareDirectionButton\"") ||
      !contains(conversation, "Show me two directions") ||
      !contains(conversation, "Compare restrained version") ||
      !contains(conversation, "Taste check · two real renders · you teach the choice") ||
      !contains(conversation, "remembered only after Commit Edits") ||
      !contains(dialog, "function photographerTasteCheck()") ||
      !contains(dialog, "function startPhotographerTasteCheck()") ||
      !contains(dialog, "directionComparisonFromCalibration") ||
      !contains(conversation, "objectName: \"aiEditDirectionCardA\"") ||
      !contains(conversation, "objectName: \"aiEditDirectionCardB\"") ||
      !contains(conversation, "objectName: \"aiEditChooseDirectionA\"") ||
      !contains(conversation, "objectName: \"aiEditChooseDirectionB\"") ||
      !contains(conversation, "objectName: \"aiEditDirectionChoiceReceipt\"") ||
      !contains(conversation, "objectName: \"aiEditExceptionReviewPanel\"") ||
      !contains(conversation, "objectName: \"aiEditExceptionReviewHeadline\"") ||
      !contains(conversation, "objectName: \"aiEditExceptionReviewList\"") ||
      !contains(conversation, "objectName: \"aiEditExceptionReviewFootnote\"") ||
      !contains(conversation, "\"aiEditExceptionFixButton\"") ||
      !contains(conversation, "objectName: exceptionCard.index === 0") ||
      !contains(conversation, "\"aiEditExceptionFirstSummary\"") ||
      !contains(conversation, "\"aiEditExceptionFirstPreserved\"") ||
      !contains(conversation, "\"aiEditExceptionKeepButton\"") ||
      !contains(conversation, "\"aiEditExceptionRevertButton\"") ||
      !contains(conversation, "Resolve \" + root.dialog.reviewIssues().length + \" first") ||
      !contains(dialog, "function exceptionReviewHeadline()") ||
      !contains(dialog, "function exceptionReviewFootnote()") ||
      !contains(dialog, "function reviewIssuePreservedSummary(issue)") ||
      !contains(dialog, "function reviewIssueAutoRepairAvailable(issue)") ||
      !contains(dialog, "function reviewIssueFixLabel(issue)") ||
      !contains(dialog, "function fixAllReviewIssues()") ||
      !contains(dialog, "function compareDirection()") ||
      !contains(dialog, "function chooseDirection(key)") ||
      !contains(dialog, "root.agentEditController.compareDraftDirection(alternative)") ||
      !contains(dialog, "root.agentEditController.chooseDraftDirection(key)")) {
    return fail("conversation should default to target, direction, and one draft action while keeping scope and inspiration in Details");
  }

  if (contains(studio, "id: agentRunDialogComponent") ||
      contains(studio, "objectName: \"agentRunDialogContent\"") ||
      contains(studio, "objectName: \"agentConversationCard\"") ||
      contains(studio, "objectName: \"agentStyleChooser\"") ||
      contains(studio, "objectName: \"agentRunAuditPanel\"")) {
    return fail("Studio should not retain the retired agent settings dialog after the live walkthrough replacement");
  }

  if (!contains(controller_header, "suggestInspirationForPrompt") ||
      !contains(controller_header, "createStyleFromDescription") ||
      !contains(controller_header, "styleReferencePreflight") ||
      !contains(controller_header, "batchReviewForecast") ||
      !contains(controller_header, "assignmentStoryMapForScope") ||
      !contains(controller_header, "reviseAssignmentStoryRole") ||
      !contains(controller_header, "rememberApprovedStoryPreference") ||
      !contains(controller_header, "forgetApprovedStoryPreference") ||
      !contains(controller_header, "storyPreferenceStatus") ||
      !contains(controller_header, "projectNorthStarStatus") ||
      !contains(controller_header, "saveProjectNorthStar") ||
      !contains(controller_header, "forgetProjectNorthStar") ||
      !contains(controller_header, "Q_PROPERTY(QString editingFreedom") ||
      !contains(controller_header, "editingFreedomPolicy") ||
      !contains(controller_header, "agent_professional_consistency_map") ||
      !contains(controller_header, "agent_consistency_exception_routing") ||
      !contains(controller_header, "agent_consistency_repair_patch") ||
      !contains(controller_header, "startDraftForScope") ||
      !contains(controller_header, "draftRunFinished") ||
      !contains(controller_header, "draftDirectionComparison") ||
      !contains(controller_header, "compareDraftDirection") ||
      !contains(controller_header, "chooseDraftDirection") ||
      !contains(controller_header, "reviseDraftAtPoint") ||
      !contains(controller_source, "local-prompt-context-ranker-v1") ||
      !contains(controller_source, "agent-draft-direction-comparison-v1") ||
      !contains(controller_source, "agent-described-style-start-v1") ||
      !contains(controller_source, "agent-style-reference-preflight-v1") ||
      !contains(controller_source, "agent-destructive-intent-seatbelt-v1") ||
      !contains(controller_source, "agent-correction-transfer-preview-v1") ||
      !contains(controller_source, "agent-editing-freedom-v1") ||
      !contains(controller_source, "style_for_editing_freedom") ||
      !contains(controller_source, "retouch_still_requires_approval") ||
      !contains(controller_source, "cluster-aware-after-sample-approval") ||
      !contains(controller_source, "agent-point-grounded-correction-v2") ||
      !contains(controller_source, "agentPointSeededContentSnap") ||
      !contains(controller_source, "point-content-snap") ||
      !contains(controller_source, "radial-fallback") ||
      !contains(controller_source, "evidenceContrastThreshold") ||
      !contains(controller_source, "point_outside_image") ||
      !contains(controller_source, "preservedOutsidePointedRegion") ||
      !contains(controller_source, "preservedOutsideResolvedTarget") ||
      !contains(controller_source, "agent-batch-review-forecast-v1") ||
      !contains(controller_source, "agent-assignment-story-map-v1") ||
      !contains(controller_source, "agent-assignment-story-role-evidence-v1") ||
      !contains(controller_source, "agent-approved-assignment-story-receipt-v1") ||
      !contains(controller_source, "agent-story-preference-memory-v1") ||
      !contains(controller_source, "agent-story-preference-remember-v1") ||
      !contains(controller_source, "agent-story-preference-forget-v1") ||
      !contains(controller_source, "future-story-role-suggestions-only") ||
      !contains(controller_source, "role-anchor-suggestion-only") ||
      !contains(controller_source, "agent-project-north-star-memory-v1") ||
      !contains(controller_source, "agent-project-north-star-save-v1") ||
      !contains(controller_source, "agent-project-north-star-forget-v1") ||
      !contains(controller_source, "agent-project-north-star-draft-receipt-v1") ||
      !contains(controller_source, "current-request-then-style-and-safety-then-north-star") ||
      !contains(controller_source, "qualitativePlanningInfluence") ||
      !contains(controller_source, "alignmentClaimed") ||
      !contains(controller_source, "requiresPhotographerJudgment") ||
      !contains(controller_source, "directNumericRecipeAuthority") ||
      !contains(local_model_source, "When projectNorthStar is available") ||
      !contains(local_model_source, "Project North Star JSON") ||
      !contains(controller_source, "review-and-delivery-receipt-only") ||
      !contains(controller_source, "validated-lighting-group-plan-unchanged-by-story-role") ||
      !contains(controller_source, "photographer-direction") ||
      !contains(controller_source, "semanticRecognitionUsed") ||
      !contains(controller_source, "agent-assignment-story-role-revision-v1") ||
      !contains(controller_source, "assignment_story_role_revision") ||
      !contains(controller_source, "remainingPhotosUntouched") ||
      !contains(controller_source, "sample-review-coverage-only") ||
      !contains(controller_source, "lighting-representatives-plus-story-anchors-before-full-batch") ||
      !contains(controller_source, "agent-batch-review-evidence-v1") ||
      !contains(controller_source, "agent-exception-review-v1") ||
      !contains(controller_source, "agent-professional-consistency-map-v1") ||
      !contains(controller_source, "within-lighting-group-relative-render-response") ||
      !contains(controller_source, "agent-consistency-reproducibility-v1") ||
      !contains(controller_source, "agent-consistency-drift-repair-v1") ||
      !contains(controller_source, "agent-consistency-exception-routing-v1") ||
      !contains(controller_source, "agent-consistency-repair-application-v1") ||
      !contains(controller_source, "Skin-sensitive consistency evidence needs a photographer decision.") ||
      !contains(controller_source, "one-batch-receipt-plus-exceptions-only") ||
      !contains(controller_source, "commitBlockedByExceptions") ||
      !contains(controller_source, "remember-only-after-confirmed-commit") ||
      !contains(controller_source, "explicit-comparison-choice-confirmed-by-commit") ||
      !contains(controller_source, "agent-async-draft-run-v2") ||
      !contains(controller_source, "agent-request-owned-planning-continuation-v2") ||
      !contains(controller_source, "QFutureWatcher<AgentLocalModelPlanResult>") ||
      !contains(controller_source, "agent-request-owned-worker-continuation-v1") ||
      !contains(controller_source, "QFutureWatcher<QVariantMap>") ||
      !contains(controller_source, "qfuture-watcher") ||
      !contains(controller_source, "active_draft_request_id_ != request_id") ||
      !contains(controller_source, "async_model_plan_fingerprint_ !=") ||
      !contains(controller_source, "continueDraftRequest(") ||
      !contains(controller_source, "agent-nonreentrant-state-publication-v1") ||
      !contains(controller_source, "agent-proof-over-thumbnails-v1") ||
      !contains(controller_source, "setInteractiveRenderingActive(true)") ||
      !contains(controller_source, "setInteractiveRenderingActive(false)") ||
      !contains(controller_source, "agent-visual-referent-v1") ||
      !contains(controller_source, "agent-visual-referent-resolution-v1") ||
      !contains(controller_source, "agent-visual-referent-fast-path-v1") ||
      !contains(controller_source, "agent-progressive-draft-preview-v1") ||
      !contains(controller_source, "agent-two-speed-planning-preview-v1") ||
      !contains(controller_source, "agent-fast-draft-independent-proof-v1") ||
      !contains(controller_source, "agent-fast-draft-hardware-deadline-v1") ||
      !contains(controller_source, "agent-validated-fast-draft-v1") ||
      !contains(controller_source, "publishFastPlanningPreview(") ||
      !contains(controller_source, "startFastPlanningPreviewValidation(") ||
      !contains(controller_source, "keepValidatedFastDraft()") ||
      !contains(controller_header, "keepValidatedFastDraft()") ||
      !contains(controller_source, "chooseTwoSpeedDraftVersion(") ||
      !contains(controller_header, "chooseTwoSpeedDraftVersion(") ||
      !contains(controller_source, "agent-two-speed-draft-choice-v1") ||
      !contains(controller_source, "agent-two-speed-taste-choice-v1") ||
      !contains(controller_source, "agent-two-speed-taste-signal-v1") ||
      !contains(controller_source, "agent-two-speed-taste-forget-v1") ||
      !contains(controller_source, "forgetTwoSpeedTastePreference(") ||
      !contains(controller_header, "forgetTwoSpeedTastePreference(") ||
      !contains(controller_source, "agent-assignment-taste-reflection-v1") ||
      !contains(controller_source, "assignmentTasteReflection()") ||
      !contains(controller_header, "assignmentTasteReflection()") ||
      !contains(controller_source, "restoreAsyncPlanningPreview(") ||
      !contains(controller_source, "firstPublishBudgetMs") ||
      !contains(controller_source, "fastPreviewReplaced") ||
      !contains(controller_source, "deepValidationPassed") ||
      !contains(controller_source, "agent-proof-worker-v1") ||
      !contains(controller_source, "agent-proof-worker-v2") ||
      !contains(controller_source, "agent-proof-worker-active-clock-v1") ||
      !contains(controller_source, "ui-pump-active-time") ||
      !contains(controller_source, "suspendedDurationMs") ||
      !contains(controller_source, "background-proof-worker") ||
      !contains(controller_source, "uiEventLoopServiced") ||
      !contains(controller_source, "controllerCallPending") ||
      !contains(controller_source, "cancellationForwarded") ||
      !contains(controller_source, "renderLoopCancellation") ||
      !contains(controller_source, "neverUpscales") ||
      !contains(controller_source, "progressivePreviewAllowed") ||
      !contains(controller_source, "commitBlockedWhilePending") ||
      !contains(controller_source, "rollbackOnFailure") ||
      !contains(controller_source, "confirmed-visual-referent-fast-path") ||
      !contains(controller_source, "modelPlanSkipped") ||
      !contains(controller_source, "transientMatteReused") ||
      !contains(controller_source, "fast-confirmed-referent-validation-preview") ||
      !contains(controller_source, "exact-project-and-asset-explicit-reference-only") ||
      !contains(controller_source, "QStringLiteral(\"headline\")") ||
      !contains(controller_source, "QStringLiteral(\"explanation\")") ||
      !contains(controller_source, "QStringLiteral(\"uiAnchor\")") ||
      !contains(controller_source, "QStringLiteral(\"characterMode\")") ||
      !contains(controller_source, "QStringLiteral(\"characterTool\")")) {
    return fail("controller should provide prompt suggestions and presentation-safe structured run events");
  }

  if (!contains(workspace_overlay, "You already have a first look.") ||
      !contains(workspace_overlay,
                "FAST PROOF PASSED · USE NOW OR WAIT · COMMIT LOCKED") ||
      !contains(workspace_overlay,
                "FAST PREVIEW · INDEPENDENT PROOF + DEEP REVIEW RUNNING · COMMIT LOCKED") ||
      !contains(workspace_overlay,
                "objectName: \"agentWorkspaceKeepValidatedFastDraftButton\"") ||
      !contains(workspace_overlay, "Use Proved Draft") ||
      !contains(workspace_overlay, "keepValidatedFastDraft()") ||
      !contains(workspace_overlay,
                "objectName: \"agentWorkspaceTwoSpeedDraftChoiceCard\"") ||
      !contains(workspace_overlay,
                "objectName: \"agentWorkspaceFirstLookDraftButton\"") ||
      !contains(workspace_overlay,
                "objectName: \"agentWorkspaceDeepReviewDraftButton\"") ||
      !contains(workspace_overlay, "chooseTwoSpeedDraftVersion(\"first-look\")") ||
      !contains(workspace_overlay, "chooseTwoSpeedDraftVersion(\"deep-review\")") ||
      !contains(workspace_overlay, "TWO PROVED VERSIONS") ||
      !contains(workspace_overlay, "Discard still restores the exact pre-agent edit.") ||
      !contains(workspace_overlay,
                "objectName: \"agentWorkspaceTwoSpeedTasteCard\"") ||
      !contains(workspace_overlay,
                "objectName: \"agentWorkspaceTwoSpeedTasteForgetButton\"") ||
      !contains(workspace_overlay, "CURRENT REQUEST WINS · NO AUTO SELECT") ||
      !contains(workspace_overlay, "forgetTwoSpeedTastePreference()") ||
      !contains(workspace_overlay,
                "objectName: \"agentWorkspaceAssignmentReflectionCard\"") ||
      !contains(workspace_overlay,
                "objectName: \"agentWorkspaceAssignmentReflectionShapeButton\"") ||
      !contains(workspace_overlay, "NOTHING REMEMBERED YET") ||
      !contains(workspace_overlay,
                "Nothing is remembered until you press Save North Star") ||
      !contains(workspace_overlay, "function fastDraftDeepTargetCountdownSeconds()") ||
      !contains(workspace_overlay, "function fastDraftDeepTargetCountdownText()") ||
      !contains(workspace_overlay, "DEEP REVIEW TARGET ") ||
      !contains(workspace_overlay, "PROVED PREVIEW · USE NOW OR WAIT") ||
      !contains(workspace_overlay,
                "String(root.progressivePreview.mode || \"\") === \"fast-planning-preview\"") ||
      !contains(workspace_overlay,
                "This fast draft passed its own image proof.")) {
    return fail("two-speed planning should expose independent fast proof, an immediate reversible choice, and the continuing deep-review target");
  }

  const auto run_event_publication = section_between(
      controller_source,
      "void AgentEditController::appendRunEvent(",
      "void AgentEditController::attachRunInteractionFeedback(");
  const auto progressive_preview_publication = section_between(
      controller_source,
      "void AgentEditController::setProgressivePreview(",
      "void AgentEditController::setPendingAgentInput(");
  const auto proof_worker_wait = section_between(
      controller_source,
      "WorkerProofResult render_agent_proof_artifacts_on_worker(",
      "QString safe_artifact_id(");
  const auto request_owned_worker_continuation = section_between(
      controller_source,
      "bool AgentEditController::takeOrStartRequestOwnedWorkerStage(",
      "bool AgentEditController::takeOrStartRequestOwnedPlanStage(");
  if (run_event_publication.empty() ||
      progressive_preview_publication.empty() ||
      proof_worker_wait.empty() ||
      request_owned_worker_continuation.empty() ||
      contains(run_event_publication, "processEvents") ||
      contains(progressive_preview_publication, "processEvents") ||
      !contains(proof_worker_wait, "processEvents") ||
      contains(request_owned_worker_continuation, "processEvents") ||
      !contains(request_owned_worker_continuation, "QFutureWatcher<QVariantMap>") ||
      !contains(controller_source, "no-resident-draft-event-pump-waits") ||
      !contains(controller_source, "logical-stage-once-per-request") ||
      !contains(controller_source, "cached-per-asset-iteration") ||
      !contains(controller_source, "project-look-model-scope-and-assets")) {
    return fail("agent state publication must be signal-only and accepted draft planning, retouch materialization, and proof rendering must use request-owned continuations without resident event-pump waits");
  }

  if (!contains(studio, "agent-workspace:v2|sequence:workspace-target-prompt-inspiration-apply") ||
      !contains(studio, "function agentTargetIdsForScope(scope)") ||
      !contains(studio, "startDraftForScope(scope, instructions)") ||
      !contains(studio, "function onDraftRunFinished(requestId, report)") ||
      !contains(studio, "function finishAgentDraftRequest(requestId, report)") ||
      contains(studio, "root.agentEditController.runDraftForScope(\n") ||
      !contains(studio, "root.page === \"edit\"") ||
      !contains(studio, "catalogAssets.projectFilter") ||
      !contains(studio, "function applyAgentResultToEditor(report)") ||
      !contains(studio, "editorPreview.applyExternalAdjustmentPatchEdit(") ||
      !contains(studio, "function runAgentDraftForScope(scope, instructions, keepAgentScreenOpen)") ||
      !contains(studio, "root.agentEditController.startDraftForScope(scope, instructions)") ||
      !contains(studio, "onDraftCommitted: function(report)") ||
      !contains(studio, "root.assetActivated(\"edit\")") ||
      !contains(studio, "AgentWorkspaceOverlay {") ||
      !contains(studio, "function focusAgentFinding(finding)") ||
      !contains(studio, "function setAgentSpatialTargetMode(enabled)") ||
      !contains(studio, "function chooseAgentSpatialTarget(normalizedX, normalizedY)") ||
      !contains(studio, "root.agentSpatialTargetRegion = ({})") ||
      !contains(studio, "component ImageViewport: StudioImageViewport {") ||
      !contains(studio, "workspaceContext: root") ||
      !contains(studio, "onSpatialTargetModeRequested: function(enabled)") ||
      !contains(studio, "function maskSetForAgentMaskId(maskId)") ||
      !contains(studio, "property var agentFindingFocusRegion") ||
      !contains(studio, "agentSelectionCandidateRegions") ||
      !contains(studio, "agentAwaitingSelectionConfirmation") ||
      !contains(workspace_overlay, "The numbered outlines are candidates. Nothing is edited until you choose.") ||
      !contains(studio_image_viewport, "externalCandidateRegions: workspaceContext.agentSelectionCandidateRegions") ||
      !contains(studio_image_viewport, "agentPointTargetingEnabled: workspaceContext.agentSpatialTargetMode") ||
      !contains(studio_image_viewport, "agentPointTargetRegion: workspaceContext.agentSpatialTargetRegion") ||
      !contains(studio_image_viewport, "onAgentPointTargeted: function(normalizedX, normalizedY)") ||
      !contains(studio_image_viewport, "workspaceContext.chooseAgentSpatialTarget(normalizedX, normalizedY)") ||
      !contains(studio, "onFindingRequested: function(finding) { root.focusAgentFinding(finding) }") ||
      !contains(studio_image_viewport, "externalFocusSerial: workspaceContext.agentFindingFocusSerial") ||
      !contains(studio_image_viewport, "externalFocusRegion: workspaceContext.agentFindingFocusRegion") ||
      !contains(studio, "editorPreview.activeMaskIndex = maskIndex") ||
      !contains(studio, "catalogAssets.selectAssetById(String(assetId), true)")) {
    return fail("Studio should infer targets before prompting, stop on a reversible draft, and apply an approved result through the real editor");
  }

  if (!contains(shared_mask_overlay, "property var externalFocusRegion") ||
      !contains(shared_mask_overlay, "readonly property bool hasExternalFocusRegion") ||
      !contains(shared_mask_overlay, "root.hasExternalFocusRegion ||") ||
      !contains(shared_mask_overlay, "root.externalFocusRegion.centerX") ||
      !contains(shared_mask_overlay, "property bool agentPointTargetingEnabled") ||
      !contains(shared_mask_overlay, "signal agentPointTargeted(real normalizedX, real normalizedY)") ||
      !contains(shared_mask_overlay, "AgentPointTargetInput") ||
      !contains(shared_mask_overlay, "TARGET SEED") ||
      !contains(shared_mask_overlay, "root.imagePointFromLocal(mouse.x, mouse.y)") ||
      !contains(shared_mask_overlay, "content-snap:confidence-gated") ||
      !contains(shared_mask_overlay, "fallback:visible-radial")) {
    return fail("agent findings should pulse their grounded evidence region even when the selected mask is semantic");
  }

  if (!contains(workspace_overlay, "objectName: \"agentWorkspaceSpeechBubble\"") ||
      !contains(workspace_overlay, "property string uiAnchor") ||
      !contains(workspace_overlay, "Behavior on x") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceControlContact\"") ||
      !contains(workspace_overlay, "root.targetResolver(root.motionEvent, key)") ||
      !contains(workspace_overlay, "Photo \" + current + \" of \"") ||
      !contains(workspace_overlay, "root.assetRequested(assetId)") ||
      !contains(workspace_overlay, "property bool applyInProgress") ||
      !contains(workspace_overlay, "signal draftCommitted(var report)") ||
      !contains(workspace_overlay, "root.draftCommitted(report)") ||
      !contains(workspace_overlay, "readonly property bool runFailed") ||
      !contains(workspace_overlay, "I couldn't finish this edit safely.") ||
      !contains(workspace_overlay, "function proofRejectionPresentation()") ||
      !contains(workspace_overlay, "function visualCritique()") ||
      !contains(workspace_overlay, "function beginRejectedRepair()") ||
      !contains(workspace_overlay, "prepareRejectedRetouchRepairAtPoint") ||
      !contains(workspace_overlay, "agentWorkspaceVisualCritiqueCard") ||
      !contains(workspace_overlay, "agentWorkspaceVisualCritiqueFinding") ||
      !contains(workspace_overlay, "agentWorkspaceVisualCritiqueEvidence") ||
      !contains(workspace_overlay, "OLD PATCH DISCARDED") ||
      !contains(workspace_overlay, "primaryActionLabel") ||
      !contains(controller_source, "agent-proof-rejection-presentation-v1") ||
      !contains(controller_source, "agent-visual-critique-v1") ||
      !contains(controller_source, "agent-proof-repair-plan-v1") ||
      !contains(controller_header, "prepareRejectedRetouchRepairAtPoint") ||
      !contains(controller_source, "I left your photo unchanged.") ||
      !contains(controller_source, "Point to repair") ||
      !contains(workspace_overlay, "function trustReceiptText()") ||
      !contains(workspace_overlay, "agentWorkspaceProgressivePreviewReceipt") ||
      !contains(workspace_overlay, "PREVIEW READY · FINAL CHECK RUNNING · COMMIT LOCKED") ||
      !contains(workspace_overlay, "Commit Edits unlocks when it passes") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceLocalPlanningHeartbeat\"") ||
      !contains(workspace_overlay, "function localPlanningReceiptText()") ||
      !contains(workspace_overlay, "NO PIXELS CHANGED") ||
      !contains(workspace_overlay, "PROVED PREVIEW · USE NOW OR WAIT") ||
      !contains(workspace_overlay, "REVERSIBLE PREVIEW LIVE · PROVING") ||
      !contains(workspace_overlay, "CANCEL SAFE") ||
      !contains(workspace_overlay, "firstAsset.groundedSelection") ||
      !contains(workspace_overlay, "awaitingSelectionConfirmation") ||
      !contains(workspace_overlay, "awaitingPlanApproval") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspacePlanApprovalDismiss\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspacePlanApprovalApprove\"") ||
      !contains(workspace_overlay, "return question.length > 0") ||
      !contains(workspace_overlay, "awaitingClarification") ||
      !contains(workspace_overlay, "awaitingSafeStop") ||
      !contains(workspace_overlay, "function pendingQuestionText()") ||
      !contains(workspace_overlay, "typeof first === \"string\"") ||
      !contains(workspace_overlay, "function submitPendingAnswer()") ||
      !contains(workspace_overlay, "agentWorkspaceClarificationCard") ||
      !contains(workspace_overlay, "agentWorkspaceClarificationPrompt") ||
      !contains(workspace_overlay, "agentWorkspaceClarificationEditor") ||
      !contains(workspace_overlay, "agentWorkspaceClarificationContinue") ||
      !contains(workspace_overlay, "Answer in your own words") ||
      !contains(workspace_overlay, "agentWorkspaceSelectionConfirmationCard") ||
      !contains(workspace_overlay, "agentWorkspaceSelectionCandidate") ||
      !contains(workspace_overlay, "confirmGroundedSelectionCandidate") ||
      !contains(workspace_overlay, "Nothing is edited until you choose") ||
      !contains(workspace_overlay, "local target boundary checked") ||
      !contains(workspace_overlay, "visible feathered target kept") ||
      !contains(workspace_overlay, "goal matched ") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTrustReceipt\"") ||
      !contains(workspace_overlay, "function continuityReceiptText()") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceContinuityReceipt\"") ||
      !contains(workspace_overlay, "Project memory · ") ||
      !contains(workspace_overlay, "agentEditController.approvedProjectTaste") ||
      !contains(workspace_overlay, "approved \" + (approvalCount === 1 ? \"look\" : \"looks\")") ||
      !contains(workspace_overlay, "agent-project-taste-mirror-v2") ||
      !contains(workspace_overlay, "import QtQuick.Dialogs") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteMirrorButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteMirrorCard\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteName\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteRenameButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteRenameEditor\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteRenameSaveButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteEvidenceRow\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteEvidenceThumbnail\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteForgetButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteCalibrationImportButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTastePassportExportButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTastePassportImportButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTastePassportPreviewCard\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTastePassportPreviewSummary\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTastePassportPreviewBoundary\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTastePassportCancelButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTastePassportConfirmButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceTasteCalibrationStatus\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationReviewCard\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationReviewButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationFinalizeButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationClipboardFinalizeButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationPacketReady\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationCoachCard\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationCoachHeadline\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationCoachExplanation\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationCoachBoundary\"") ||
      !contains(workspace_overlay, "function photographerCalibrationCoach()") ||
      !contains(workspace_overlay, "CALIBRATION COACH") ||
      !contains(workspace_overlay, "NO AUTO TRAINING · NO THRESHOLD CHANGE") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCalibrationReviewBoundary\"") ||
      !contains(workspace_overlay, "function openPhotographerCalibrationReview(fileUrl)") ||
      !contains(workspace_overlay, "openPhotographerCalibrationReviewPacketFile") ||
      !contains(workspace_overlay, "function beginPhotographerCalibrationFinalize()") ||
      !contains(workspace_overlay, "function finishCopiedPhotographerCalibrationRatings()") ||
      !contains(workspace_overlay, "function finishPhotographerCalibrationRatings(ratingsUrl)") ||
      !contains(workspace_overlay, "property string activeCalibrationReviewProjectKey") ||
      !contains(workspace_overlay, "root.activeCalibrationReviewProjectKey === activeProjectKey") ||
      !contains(workspace_overlay, "function finalizePhotographerCalibration(ratingsUrl, packetUrl)") ||
      !contains(workspace_overlay, "finalizePhotographerCalibrationReviewFiles") ||
      !contains(workspace_overlay, "finalizePhotographerCalibrationReviewClipboard") ||
      !contains(workspace_overlay, "COPY FINISH IS ONE CLICK") ||
      !contains(workspace_overlay, "clipboard cleared") ||
      !contains(workspace_overlay, "agent-photographer-calibration-session-handoff-v1") ||
      !contains(workspace_overlay, "COPY FINISH IS ONE CLICK") ||
      !contains(workspace_overlay, "VERIFIED PACKET KEPT FOR THIS SESSION") ||
      !contains(workspace_overlay, "source manifest kept unchanged") ||
      !contains(workspace_overlay, "HUMAN-ONLY GATE") ||
      !contains(workspace_overlay, "PACKET VERIFIED BEFORE OPEN") ||
      !contains(workspace_overlay, "AGENT CANNOT RATE") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceNorthStarCard\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceNorthStarBrief\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceNorthStarEditor\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceNorthStarSaveButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceNorthStarForgetButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceNorthStarDraftReceipt\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceNorthStarDraftBrief\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceNorthStarDraftProof\"") ||
      !contains(workspace_overlay, "function projectNorthStarDraftReceipt()") ||
      !contains(workspace_overlay, "ASSIGNMENT COMPASS") ||
      !contains(workspace_overlay, "USED IN PLAN · CURRENT REQUEST LED") ||
      !contains(workspace_overlay, "North Star fit is your call") ||
      !contains(workspace_overlay, "NO AUTO CULL · NO AUTO RETOUCH") ||
      !contains(workspace_overlay, "function saveNorthStar()") ||
      !contains(workspace_overlay, "function forgetNorthStar()") ||
      !contains(workspace_overlay, "FUTURE DRAFTS · CURRENT REQUEST WINS") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspacePhotographerTasteCheckCard\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspacePhotographerTasteCheckAction\"") ||
      !contains(workspace_overlay, "CALIBRATED TASTE CHECK") ||
      !contains(workspace_overlay, "2 REAL RENDERS · LEARNED ONLY AFTER CHOICE + COMMIT") ||
      !contains(workspace_overlay, "compareDraftWithPhotographerCalibration") ||
      !contains(workspace_overlay, "function tasteEvidenceThumbnail(evidence)") ||
      !contains(workspace_overlay, "function photographerCalibration()") ||
      !contains(workspace_overlay, "importPhotographerCalibrationProfileFile") ||
      !contains(workspace_overlay, "function importPhotographerTastePassport(fileUrl)") ||
      !contains(workspace_overlay, "function confirmPhotographerTastePassport()") ||
      !contains(workspace_overlay, "function cancelPhotographerTastePassport()") ||
      !contains(workspace_overlay, "function exportPhotographerTastePassport(fileUrl)") ||
      !contains(workspace_overlay, "TASTE PASSPORT · CALIBRATION ONLY · NO PHOTOS OR PATHS") ||
      !contains(workspace_overlay, "VERIFIED TASTE PASSPORT") ||
      !contains(workspace_overlay, "Use for this project") ||
      !contains(workspace_overlay, "REPLACES CURRENT CALIBRATION · THIS PROJECT ONLY · EDITS UNCHANGED") ||
      !contains(workspace_overlay, "Passport provenance verified · active project only") ||
      !contains(workspace_overlay, "CALIBRATED") ||
      !contains(workspace_overlay, "catalogModel.assetById(normalizedId)") ||
      !contains(workspace_overlay, "function saveTasteName()") ||
      !contains(workspace_overlay, "renameProjectTaste(name)") ||
      !contains(workspace_overlay, "function requestTasteForget(evidence)") ||
      !contains(workspace_overlay, "forgetProjectTasteEvidence(") ||
      !contains(workspace_overlay, "THIS PROJECT ONLY") ||
      !contains(workspace_overlay, "NO IMAGE BYTES") ||
      !contains(workspace_overlay, "EDITS UNCHANGED WHEN FORGOTTEN") ||
      !contains(workspace_overlay, "agent-edit-strategy-route-v1") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceEditStrategyCard\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceEditStrategyAuthority\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceEditStrategyLabel\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceEditStrategyReason\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceEditStrategyPreserved\"") ||
      !contains(workspace_overlay, "EDIT APPROACH") ||
      !contains(workspace_overlay, "CHOSEN FOR THIS PHOTO") ||
      !contains(workspace_overlay, "PROTECTED · ") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCorrectionCard\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspacePointToAreaButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceSubmitCorrectionButton\"") ||
      !contains(workspace_overlay, "function beginSpatialCorrection()") ||
      !contains(workspace_overlay, "function acceptSpatialTarget(normalizedX, normalizedY, assetId)") ||
      !contains(workspace_overlay, "reviseDraftAtPoint(") ||
      !contains(workspace_overlay, "root.findingRequested(spatial)") ||
      !contains(workspace_overlay, "Target: content at this point · feathered fallback if unclear") ||
      !contains(workspace_overlay, "snapped to local content · outside it protected") ||
      !contains(workspace_overlay, "feathered fallback · no object claim") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspacePointResolutionCard\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspacePointResolutionLabel\"") ||
      !contains(workspace_overlay, "CONTENT BOUNDARY FOUND · CLICK INCLUDED · OUTSIDE PROTECTED") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCorrectionEditor\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceCorrectionScope\"") ||
      !contains(workspace_overlay, "Changing only: ") ||
      !contains(workspace_overlay, "reviseDraftFinding(root.activeFinding, feedback)") ||
      !contains(workspace_overlay, "only \" + targetLabel + \" revised") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceVisualFindingsCard\"") ||
      !contains(workspace_overlay, "agent-editable-intent-ledger-v1") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceIntentLedger\"") ||
      !contains(workspace_overlay, "function findingForLocalizedAction(action)") ||
      !contains(workspace_overlay, "function protectedIntentLabels()") ||
      !contains(workspace_overlay, "function openIntentCorrection(action)") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceIntentAction\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceIntentChangeButton\"") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceIntentProtection\"") ||
      !contains(workspace_overlay, "function visualFindings()") ||
      !contains(workspace_overlay, "function localizedActions()") ||
      !contains(workspace_overlay, "signal findingRequested(var finding)") ||
      !contains(workspace_overlay, "function requestFinding(finding)") ||
      !contains(workspace_overlay, "findingDelegate.modelData.targetLabel") ||
      !contains(workspace_overlay, "objectName: \"agentWorkspaceFindingRow\"") ||
      !contains(workspace_overlay, "onClicked: root.requestFinding(findingDelegate.modelData)") ||
      !contains(workspace_overlay, "text: \"WHAT I SAW\"") ||
      !contains(workspace_overlay, "text: \"WHAT I'LL CHANGE\"") ||
      !contains(workspace_overlay, "root.localizedActions().length + \" local \"") ||
      !contains(workspace_overlay, "root.agentEditController.reviseDraft(feedback)") ||
      !contains(workspace_overlay, "rollbackDraft(\"user_discarded\")") ||
      !contains(workspace_overlay, "learned \" + (correctionCount === 1 ? \"correction\" : \"corrections\")")) {
    return fail("workspace character should move, talk, follow each target, preserve explicit draft approval, and show truthful proof and project-memory receipts");
  }

  if (!contains(local_model_source, "recentVisualReferents") ||
      !contains(local_model_source, "exact active asset") ||
      !contains(local_model_source, "never resolve a vague pronoun by guessing")) {
    return fail("the local planning prompt should treat visual referents as exact-asset user confirmations, never transferable guesses");
  }

  if (!contains(shared_mask_overlay, "property int externalFocusSerial: 0") ||
      !contains(shared_mask_overlay, "onExternalFocusSerialChanged:") ||
      !contains(shared_mask_overlay, "id: agentFindingFocusPulse") ||
      !contains(shared_mask_overlay, "id: agentFindingFocusAnimation") ||
      !contains(shared_mask_overlay, "property var externalCandidateRegions") ||
      !contains(shared_mask_overlay, "AgentGroundingCandidateBadge")) {
    return fail("agent grounding should pulse linked findings and draw numbered candidate regions before an ambiguous instance is confirmed");
  }

  if (!contains(controller_header, "Q_PROPERTY(QVariantMap approvedProjectTaste") ||
      !contains(controller_header, "Q_PROPERTY(QVariantMap photographerCalibrationProfile") ||
      !contains(controller_header, "Q_PROPERTY(QVariantMap photographerCalibrationCoach") ||
      !contains(controller_header, "importPhotographerCalibrationProfileFile") ||
      !contains(controller_header, "exportPhotographerTastePassportFile") ||
      !contains(controller_header, "inspectPhotographerTastePassportFile") ||
      !contains(controller_header, "importPhotographerTastePassportFile") ||
      !contains(controller_header, "openPhotographerCalibrationReviewPacketFile") ||
      !contains(controller_header, "finalizePhotographerCalibrationReviewFiles") ||
      !contains(controller_header, "finalizePhotographerCalibrationReviewClipboard") ||
      !contains(controller_header, "forgetPhotographerCalibrationProfile") ||
      !contains(controller_header, "forgetProjectTasteEvidence") ||
      !contains(controller_header, "Q_INVOKABLE QVariantMap reviseDraftFinding") ||
      !contains(controller_header, "confirmGroundedSelectionCandidate") ||
      !contains(controller_source, "agent-targeted-correction-v1") ||
      !contains(controller_source, "agent-grounded-selection-confirmation-v1") ||
      !contains(controller_source, "agent-photographer-taste-passport-v1") ||
      !contains(controller_source, "agent-photographer-taste-passport-export-v1") ||
      !contains(controller_source, "agent-photographer-taste-passport-preview-v1") ||
      !contains(controller_source, "agent-photographer-taste-passport-import-v1") ||
      !contains(controller_source, "Taste Passport changed after preview") ||
      !contains(controller_source, "explicitImportRequired") ||
      !contains(controller_source, "sourceProjectFingerprintSha256") ||
      !contains(controller_source, "no-edit-before-explicit-instance-confirmation") ||
      !contains(controller_source, "apply-original-plan-with-confirmed-instance-no-replan") ||
      !contains(local_model_source, "selectionCandidates") ||
      !contains(local_model_source, "do not guess silently") ||
      !contains(controller_source, "generatedMattePreserved") ||
      !contains(controller_source, "preservedUnrelatedEdits") ||
      !contains(controller_source, "forceCurrentRecipeBase") ||
      !contains(controller_source, "persistApprovedProjectTaste(report)") ||
      !contains(controller_source, "explicit-revision-confirmed-by-commit") ||
      !contains(controller_source, "explicit-user-discard") ||
      !contains(controller_source, "agent-photographer-calibration-native-finalize-v1") ||
      !contains(controller_source, "agent-photographer-calibration-clipboard-handoff-v1") ||
      !contains(controller_source, "ratingsFileRequired") ||
      !contains(controller_source, "clipboardPayloadRetained") ||
      !contains(controller_source, "agent-photographer-calibration-coach-v1") ||
      !contains(controller_source, "photographerDecisionIsAuthority") ||
      !contains(controller_source, "automaticModelTrainingAllowed") ||
      !contains(controller_source, "sourceManifestOverwritten") ||
      !contains(controller_source, "automaticThresholdMutationAllowed") ||
      !contains(controller_source, "promote-only-after-confirmed-revised-draft-commit") ||
      !contains(controller_source, "exact-active-project-only") ||
      !contains(controller_source, "confirmed-agent-commit")) {
    return fail("confirmed drafts should become bounded, project-isolated taste evidence for future agent plans");
  }

  if (!contains(controller_source, "agent-project-photographer-calibration-v1") ||
      !contains(controller_source, "agent-photographer-calibration-card-v1") ||
      !contains(controller_source, "agent-photographer-calibration-review-launch-v1") ||
      !contains(controller_source, "agent-human-review-packet-v2") ||
      !contains(controller_source, "agent-blind-light-table-v1") ||
      !contains(controller_source, "machineEvidenceEmbeddedInReviewPage") ||
      !contains(controller_source, "proofHashesVerified") ||
      !contains(controller_source, "machineCanCompleteRatings") ||
      !contains(controller_source, "agent-photographer-taste-check-v1") ||
      !contains(controller_source, "calibration-sensitive-draft") ||
      !contains(controller_source, "explicit-direction-choice-confirmed-by-commit-only") ||
      !contains(controller_source, "tasteTeachingSignal") ||
      !contains(controller_header, "compareDraftWithPhotographerCalibration") ||
      !contains(controller_source, "numericThresholdOverridesAllowed") ||
      !contains(controller_source, "freshCorpusRunRequiredAfterThresholdChange") ||
      !contains(local_model_source, "photographerCalibrationCard") ||
      !contains(local_model_source, "never invent or change numeric thresholds") ||
      !contains(local_model_source, "professional readiness")) {
    return fail("blind photographer calibration should guide project-local planning and critique without weakening proof thresholds");
  }

  if (!contains(controller_source, "agent-project-taste-mirror-v2") ||
      !contains(controller_source, "agent-project-taste-evidence-v1") ||
      !contains(controller_source, "agent-project-taste-rename-v1") ||
      !contains(controller_source, "agent-project-taste-forget-v1") ||
      !contains(controller_source, "visualExemplarsByCatalogId") ||
      !contains(controller_source, "exclude-from-future-project-planning") ||
      !contains(controller_source, "activeDraftUnaffected") ||
      !contains(controller_source, "forgetRequiresConfirmation")) {
    return fail("project taste should be inspectable and individually forgettable without changing existing edits");
  }

  if (!contains(controller_source, "agent-edit-strategy-route-v1") ||
      !contains(controller_source, "least-invasive-capable-path-before-draft") ||
      !contains(controller_source, "photo-editor-app") ||
      !contains(controller_source, "edit_strategy_inconsistent") ||
      !contains(controller_source, "Per-photo repair") ||
      !contains(controller_source, "Cluster-aware batch") ||
      !contains(controller_source, "Protected retouch") ||
      !contains(controller_source, "Localized adjustment") ||
      !contains(controller_source, "Composition adjustment") ||
      !contains(controller_source, "Global color and tone")) {
    return fail("the app should route each plan through the least-invasive capable edit path before creating a draft");
  }

  if (!contains(agent_light, "looksy-light:v3|form:rgb-optical-orb|render:photographic-light-asset") ||
      !contains(looksy_mark, "looksy-mark:v4|asset:period-crop|source:provided-looksy-logo") ||
      !contains(looksy_character, "looksy-agent-character:v6|form:single-body-line-character") ||
      !contains(looksy_character, "CrtSignalSurface {") ||
      !contains(looksy_character, "property bool celebrating") ||
      !contains(looksy_character, "celebration:small-nod") ||
      !contains(looksy_character, "Accessible.name: \"Looksy visual assistant, \" + mode") ||
      !contains(dialog, "objectName: \"agentRobotScreenArtwork\"") ||
      !contains(assistant_dock, "LooksyAgentCharacter {") ||
      !contains(assistant_dock, "looksy-agent-entry:v5|placement:left-rail") ||
      !contains(assistant_dock, "glyphOnly: true") ||
      !contains(assistant_dock, "HoverHandler {")) {
    return fail("Looksy's character should retain accessible entry and restrained CRT reactions");
  }

  if (!contains(progress_strip, "progress-strip:v4|channels:soft-color|composite:additive|scope:leading-glint|motion:eased-sweep|reduced-motion:static") ||
      !contains(progress_strip, "globalCompositeOperation = \"lighter\"") ||
      !contains(progress_strip, "const colors = [") ||
      !contains(progress_strip, "Easing.InOutSine") ||
      !contains(main_qml, "activity-progress-placement:v2|header-overlay:true|layout-shift:false|text:false") ||
      !contains(main_qml, "objectName: \"additiveActivityProgressLine\"") ||
      !contains(main_qml, "visible: (window.appActivityErrorVisible || window.recoveryBannerVisible)")) {
    return fail("Loading should stay in the fixed header with a restrained eased light sweep and no layout shift");
  }

  if (!contains(workspace_overlay, "property bool traveling: false") ||
      !contains(workspace_overlay, "property bool speaking: false") ||
      !contains(workspace_overlay, "id: robotTravel") ||
      !contains(workspace_overlay, "root.followSurfaceRoute()") ||
      !contains(workspace_overlay, "root.traveling = false") ||
      !contains(workspace_overlay, "if (mode === \"adjust\" || mode === \"processing\")") ||
      !contains(workspace_overlay, "function assistantLightMode()") ||
      !contains(workspace_overlay, "function assistantLightTool()") ||
      !contains(workspace_overlay, "LooksyAgentCharacter {") ||
      !contains(workspace_overlay, "tool: root.deployedTool") ||
      !contains(workspace_overlay, "talking: root.speaking && !root.traveling") ||
      !contains(controller_source, "character_mode = QStringLiteral(\"observe\")") ||
      !contains(controller_source, "character_mode = QStringLiteral(\"review\")")) {
    return fail("character motion should walk between anchors, speak briefly on arrival, and animate semantically for each agent stage");
  }

  return EXIT_SUCCESS;
}
