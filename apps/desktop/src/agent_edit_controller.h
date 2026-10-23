#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QMetaObject>
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>

#include "agent_edit_style.h"
#include "agent_local_model.h"

namespace photo_editor::desktop {

class CatalogAssetModel;
class ExportJobController;
class ManagedAgentModelDownload;
struct EditRecipeState;

[[nodiscard]] QVariantMap agent_exception_review_summary(
    int asset_count,
    int edited_count,
    int passed_count,
    int needs_review_count,
    int failed_count,
    const QVariantList& review_issues);

[[nodiscard]] QVariantMap agent_professional_consistency_map(
    const QVariantList& asset_reports);

[[nodiscard]] QVariantMap agent_consistency_exception_routing(
    const QVariantMap& consistency_map,
    const QVariantList& asset_reports,
    const QVariantList& existing_review_issues);

[[nodiscard]] QVariantMap agent_consistency_repair_patch(
    const QVariantMap& source_patch,
    const QVariantMap& repair_guidance);

[[nodiscard]] QVariantMap agent_proof_worker_active_clock_sample(
    qint64 observed_interval_ms,
    qint64 expected_interval_ms = 5);

[[nodiscard]] QVariantList agent_planning_tool_schemas_for_approved_task(
    const QVariantList& phase_schemas,
    const QVariantMap& approved_snapshot);

[[nodiscard]] bool agent_approved_task_has_explicit_photo_quality_scope(
    const QVariantMap& approved_snapshot);

namespace agent_detail {
// Shared native completion verifier; independent evidence checks can exercise
// the same admission path used by plan construction and draft application.
[[nodiscard]] QVariantMap native_composition_completion_evidence(
    const QVariantMap& prompt_audit, const QVariantMap& edit_intent,
    const QVariantMap& recipe_patch, const QVariantMap& model_tool_plan,
    const QString& asset_id, int asset_count);
}  // namespace agent_detail

class AgentEditController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(bool canRevertTask READ canRevertTask NOTIFY taskTransactionChanged)
  Q_PROPERTY(bool cancellationRequested READ cancellationRequested NOTIFY cancellationChanged)
  Q_PROPERTY(QVariantMap taskTransaction READ taskTransaction NOTIFY taskTransactionChanged)
  Q_PROPERTY(bool manualEditingActive READ manualEditingActive WRITE setManualEditingActive NOTIFY manualEditingActiveChanged)
  Q_PROPERTY(int runTimeoutMs READ runTimeoutMs WRITE setRunTimeoutMs NOTIFY runTimeoutMsChanged)
  Q_PROPERTY(bool draftActive READ draftActive NOTIFY draftChanged)
  Q_PROPERTY(QVariantMap draftDirectionComparison READ draftDirectionComparison NOTIFY draftChanged)
  Q_PROPERTY(QString selectedStyleId READ selectedStyleId WRITE setSelectedStyleId NOTIFY selectedStyleChanged)
  Q_PROPERTY(QString selectedModelId READ selectedModelId WRITE setSelectedModelId NOTIFY selectedModelChanged)
  Q_PROPERTY(QString userInstructions READ userInstructions WRITE setUserInstructions NOTIFY userInstructionsChanged)
  Q_PROPERTY(QString recipeBaseMode READ recipeBaseMode WRITE setRecipeBaseMode NOTIFY recipeBaseModeChanged)
  Q_PROPERTY(QString editingFreedom READ editingFreedom WRITE setEditingFreedom NOTIFY editingFreedomChanged)
  Q_PROPERTY(QVariantList availableStyles READ availableStyles NOTIFY availableStylesChanged)
  Q_PROPERTY(QVariantList availableModels READ availableModels NOTIFY availableModelsChanged)
  Q_PROPERTY(bool modelProbeRunning READ modelProbeRunning NOTIFY modelProbeChanged)
  Q_PROPERTY(QStringList availableModelNames READ availableModelNames NOTIFY availableModelsChanged)
  Q_PROPERTY(QString mcpEndpoint READ mcpEndpoint NOTIFY mcpStatusChanged)
  Q_PROPERTY(QString mcpEndpointKind READ mcpEndpointKind NOTIFY mcpStatusChanged)
  Q_PROPERTY(QString mcpStatus READ mcpStatus NOTIFY mcpStatusChanged)
  Q_PROPERTY(QString mcpLastError READ mcpLastError NOTIFY mcpStatusChanged)
  Q_PROPERTY(int mcpToolCount READ mcpToolCount NOTIFY mcpStatusChanged)
  Q_PROPERTY(QStringList mcpToolNames READ mcpToolNames NOTIFY mcpStatusChanged)
  Q_PROPERTY(bool mcpAvailable READ mcpAvailable NOTIFY mcpStatusChanged)
  Q_PROPERTY(int mcpConnectedClientCount READ mcpConnectedClientCount NOTIFY mcpStatusChanged)
  Q_PROPERTY(int mcpTotalClientCount READ mcpTotalClientCount NOTIFY mcpStatusChanged)
  Q_PROPERTY(QString mcpLastToolCallName READ mcpLastToolCallName NOTIFY mcpStatusChanged)
  Q_PROPERTY(QString mcpLastToolCallStatus READ mcpLastToolCallStatus NOTIFY mcpStatusChanged)
  Q_PROPERTY(QString mcpLastToolCallAt READ mcpLastToolCallAt NOTIFY mcpStatusChanged)
  Q_PROPERTY(bool modelInstallRunning READ modelInstallRunning NOTIFY modelInstallChanged)
  Q_PROPERTY(QString modelInstallStatus READ modelInstallStatus NOTIFY modelInstallChanged)
  Q_PROPERTY(QString modelInstallOutput READ modelInstallOutput NOTIFY modelInstallChanged)
  Q_PROPERTY(QVariantMap modelInstallProgress READ modelInstallProgress NOTIFY modelInstallChanged)
  Q_PROPERTY(QVariantMap lastRunReport READ lastRunReport NOTIFY lastRunReportChanged)
  Q_PROPERTY(QVariantMap progressivePreview READ progressivePreview NOTIFY progressivePreviewChanged)
  Q_PROPERTY(QString activeDraftRequestId READ activeDraftRequestId NOTIFY draftRequestChanged)
  Q_PROPERTY(QString activeTaskId READ activeTaskId NOTIFY draftRequestChanged)
  Q_PROPERTY(bool draftRequestPending READ draftRequestPending NOTIFY draftRequestChanged)
  Q_PROPERTY(QVariantList runEvents READ runEvents NOTIFY runEventsChanged)
  Q_PROPERTY(QVariantList runHistory READ runHistory NOTIFY runHistoryChanged)
  Q_PROPERTY(QVariantMap approvedProjectTaste READ approvedProjectTaste NOTIFY projectTasteChanged)
  Q_PROPERTY(QVariantMap assignmentTasteReflection READ assignmentTasteReflection NOTIFY projectTasteChanged)
  Q_PROPERTY(QVariantMap photographerCalibrationProfile READ photographerCalibrationProfile NOTIFY projectTasteChanged)
  Q_PROPERTY(QVariantMap photographerCalibrationCoach READ photographerCalibrationCoach NOTIFY projectTasteChanged)
  Q_PROPERTY(bool awaitingUserInput READ awaitingUserInput NOTIFY pendingAgentInputChanged)
  Q_PROPERTY(QVariantList pendingUserQuestions READ pendingUserQuestions NOTIFY pendingAgentInputChanged)
  Q_PROPERTY(QString safeStopReason READ safeStopReason NOTIFY pendingAgentInputChanged)

 public:
  explicit AgentEditController(QObject* parent = nullptr);
  void setAccessPolicy(std::function<bool()> policy) { access_policy_ = std::move(policy); }

  void setCatalogModel(CatalogAssetModel* catalog_model);
  void setExportJobController(ExportJobController* export_job_controller);

  [[nodiscard]] QString status() const;
  [[nodiscard]] QString errorMessage() const;
  [[nodiscard]] bool running() const;
  [[nodiscard]] bool canRevertTask() const;
  [[nodiscard]] bool cancellationRequested() const;
  [[nodiscard]] bool manualEditingActive() const;
  [[nodiscard]] int runTimeoutMs() const;
  [[nodiscard]] bool draftActive() const;
  [[nodiscard]] QVariantMap draftDirectionComparison() const;
  [[nodiscard]] QString selectedStyleId() const;
  [[nodiscard]] QString selectedModelId() const;
  [[nodiscard]] QString userInstructions() const;
  [[nodiscard]] QString recipeBaseMode() const;
  [[nodiscard]] QString editingFreedom() const;
  [[nodiscard]] QVariantList availableStyles() const;
  [[nodiscard]] QVariantList availableModels() const;
  [[nodiscard]] bool modelProbeRunning() const;
  [[nodiscard]] QStringList availableModelNames() const;
  [[nodiscard]] QString mcpEndpoint() const;
  [[nodiscard]] QString mcpEndpointKind() const;
  [[nodiscard]] QString mcpStatus() const;
  [[nodiscard]] QString mcpLastError() const;
  [[nodiscard]] int mcpToolCount() const;
  [[nodiscard]] QStringList mcpToolNames() const;
  [[nodiscard]] bool mcpAvailable() const;
  [[nodiscard]] int mcpConnectedClientCount() const;
  [[nodiscard]] int mcpTotalClientCount() const;
  [[nodiscard]] QString mcpLastToolCallName() const;
  [[nodiscard]] QString mcpLastToolCallStatus() const;
  [[nodiscard]] QString mcpLastToolCallAt() const;
  [[nodiscard]] bool modelInstallRunning() const;
  [[nodiscard]] QString modelInstallStatus() const;
  [[nodiscard]] QString modelInstallOutput() const;
  [[nodiscard]] QVariantMap modelInstallProgress() const;
  [[nodiscard]] QVariantMap lastRunReport() const;
  [[nodiscard]] QVariantMap progressivePreview() const;
  [[nodiscard]] QString activeDraftRequestId() const;
  [[nodiscard]] QString activeTaskId() const;
  [[nodiscard]] bool draftRequestPending() const;
  [[nodiscard]] QVariantList runEvents() const;
  [[nodiscard]] QVariantList runHistory() const;
  [[nodiscard]] QVariantMap approvedProjectTaste() const;
  [[nodiscard]] QVariantMap assignmentTasteReflection() const;
  [[nodiscard]] QVariantMap photographerCalibrationProfile() const;
  [[nodiscard]] QVariantMap photographerCalibrationCoach() const;
  [[nodiscard]] bool awaitingUserInput() const;
  [[nodiscard]] QVariantList pendingUserQuestions() const;
  [[nodiscard]] QString safeStopReason() const;

  void setSelectedStyleId(const QString& style_id);
  void setSelectedModelId(const QString& model_id);
  void setUserInstructions(const QString& instructions);
  void setRecipeBaseMode(const QString& mode);
  void setEditingFreedom(const QString& mode);
  void setRunTimeoutMs(int timeout_ms);
  void setManualEditingActive(bool active);
  void setTaskTransactionHandler(std::function<QVariantMap(const QString&, const QString&, const QStringList&, const QVariantMap&)> handler) {
    task_transaction_handler_ = std::move(handler);
  }
  [[nodiscard]] QVariantMap setApprovedTaskSnapshot(const QVariantMap& snapshot);
  [[nodiscard]] QVariantMap approveTaskRevision(
      const QVariantMap& snapshot,
      const QString& expected_task_id);
  void setPlanningSnapshotHandler(std::function<QVariantMap(const QString&)> handler) {
    planning_snapshot_handler_ = std::move(handler);
  }
  [[nodiscard]] QVariantMap taskTransaction() const { return task_transaction_state_; }
  [[nodiscard]] QVariantMap pendingTaskFinalization() const { return pending_task_finalization_; }
  [[nodiscard]] QVariantMap settleTaskFinalization(
      const QString& task_id,
      const QVariantMap& receipt);
  Q_INVOKABLE QVariantMap revertTask();
  void setMcpEndpoint(
      const QString& endpoint,
      const QString& endpoint_kind,
      const QString& bearer_token = {});

  Q_INVOKABLE void reloadStyles();
  Q_INVOKABLE void reloadModels();
  Q_INVOKABLE QVariantMap refreshMcpTools();
  Q_INVOKABLE QVariantMap prepareSelectedModelInstall() const;
  Q_INVOKABLE QVariantMap setupLocalAi(bool confirmed = false);
  Q_INVOKABLE QVariantMap installSelectedModel(bool confirmed = false);
  Q_INVOKABLE void cancelModelInstall();
  Q_INVOKABLE void retryLocalRuntime();
  Q_INVOKABLE QVariantMap probeSelectedModel();
  Q_INVOKABLE void probeSelectedModelAsync(bool force = false);
  Q_INVOKABLE QVariantMap validateStyleJson(const QString& style_json) const;
  Q_INVOKABLE bool saveStyle(const QVariantMap& style);
  Q_INVOKABLE QVariantMap exportStyle(
      const QString& style_id = QString(),
      const QString& destination_directory = QString());
  Q_INVOKABLE QVariantMap importStyleJson(
      const QString& style_json,
      const QString& name_override = QString());
  Q_INVOKABLE QVariantMap mcpToolPolicyForPhase(
      const QString& phase = QStringLiteral("sample")) const;
  Q_INVOKABLE QVariantMap createStyle(
      const QString& name,
      const QString& goal,
      const QVariantMap& allowed_tools,
      const QVariantMap& constraints,
      const QVariantMap& validation,
      const QVariantMap& batch_behavior);
  Q_INVOKABLE QVariantMap createStyleFromDescription(
      const QString& description,
      const QString& name = QString());
  Q_INVOKABLE QVariantMap createLearnedStyleProfileFromSelection(
      const QString& name,
      const QString& goal,
      const QString& notes = QString());
  Q_INVOKABLE QVariantMap createLearnedStyleProfileFromAssets(
      const QString& name,
      const QString& goal,
      const QString& notes,
      const QStringList& asset_ids);
  Q_INVOKABLE QVariantMap styleReferencePreflight(
      const QStringList& asset_ids = QStringList()) const;
  Q_INVOKABLE QVariantMap planStyleProjectRun(const QString& scope = QStringLiteral("selected-assets"));
  Q_INVOKABLE QVariantMap batchReviewForecast(
      const QString& scope = QStringLiteral("selected-assets")) const;
  Q_INVOKABLE QVariantMap assignmentStoryMapForScope(
      const QString& scope = QStringLiteral("selected-assets")) const;
  Q_INVOKABLE QVariantMap reviseAssignmentStoryRole(
      const QString& asset_id,
      const QString& role);
  Q_INVOKABLE QVariantMap storyPreferenceStatus() const;
  Q_INVOKABLE QVariantMap rememberApprovedStoryPreference(
      bool confirmed = false);
  Q_INVOKABLE QVariantMap forgetApprovedStoryPreference(
      bool confirmed = false);
  Q_INVOKABLE QVariantMap recordStyleFeedback(
      const QString& feedback,
      const QVariantMap& context = QVariantMap());
  Q_INVOKABLE QVariantMap memoryVaultStatus() const;
  Q_INVOKABLE QVariantMap exportMemoryVault(
      const QString& destination_directory = QString(),
      bool redacted = true) const;
  Q_INVOKABLE QVariantMap openMemoryVault() const;
  Q_INVOKABLE QVariantMap openMemoryVaultInObsidian(bool dry_run = false) const;
  Q_INVOKABLE QVariantMap resetMemoryVault(bool confirmed = false);
  Q_INVOKABLE QVariantMap memoryPacketForPhase(
      const QString& phase = QStringLiteral("sample")) const;
  Q_INVOKABLE QVariantMap projectNorthStarStatus() const;
  Q_INVOKABLE QVariantMap saveProjectNorthStar(const QString& brief);
  Q_INVOKABLE QVariantMap forgetProjectNorthStar(bool confirmed = false);
  Q_INVOKABLE QVariantMap renameProjectTaste(const QString& display_name);
  Q_INVOKABLE QVariantMap importPhotographerCalibrationProfile(
      const QString& profile_json);
  Q_INVOKABLE QVariantMap importPhotographerCalibrationProfileFile(
      const QString& path_or_url);
  Q_INVOKABLE QVariantMap exportPhotographerTastePassportFile(
      const QString& path_or_url) const;
  Q_INVOKABLE QVariantMap inspectPhotographerTastePassportFile(
      const QString& path_or_url) const;
  Q_INVOKABLE QVariantMap importPhotographerTastePassportFile(
      const QString& path_or_url,
      const QString& expected_passport_digest = QString());
  Q_INVOKABLE QVariantMap openPhotographerCalibrationReviewPacketFile(
      const QString& path_or_url,
      bool dry_run = false) const;
  Q_INVOKABLE QVariantMap finalizePhotographerCalibrationReviewFiles(
      const QString& ratings_path_or_url,
      const QString& packet_path_or_url);
  Q_INVOKABLE QVariantMap finalizePhotographerCalibrationReviewClipboard(
      const QString& packet_path_or_url);
  Q_INVOKABLE QVariantMap forgetPhotographerCalibrationProfile(
      bool confirmed = false);
  Q_INVOKABLE QVariantMap photographerTasteCheck() const;
  Q_INVOKABLE QVariantMap forgetProjectTasteEvidence(
      const QString& kind,
      const QString& evidence_id,
      bool confirmed = false);
  Q_INVOKABLE QVariantMap forgetTwoSpeedTastePreference(
      bool confirmed = false);
  Q_INVOKABLE QVariantMap suggestInspirationForPrompt(const QString& prompt) const;
  Q_INVOKABLE QVariantMap aiEditWorkflowState(
      const QString& scope = QString()) const;
  Q_INVOKABLE QVariantMap editingFreedomPolicy() const;
  QVariantMap authorizeNativeEditingCommand(const QString& name, bool property) const;
  Q_INVOKABLE QVariantMap validateRecipePatch(const QVariantMap& patch) const;
  Q_INVOKABLE QVariantMap analyzeSelected();
  Q_INVOKABLE QVariantMap renderProofForSelected();
  Q_INVOKABLE QVariantMap validateBatchForSelected();
  Q_INVOKABLE QVariantMap validateBatchForScope(const QString& scope);
  Q_INVOKABLE QVariantMap exportPreflightForSelected(
      const QString& preset = QString(),
      const QString& destination_directory = QString(),
      const QVariantMap& metrics = QVariantMap());
  Q_INVOKABLE QVariantMap exportPreflightForScope(
      const QString& scope,
      const QString& preset = QString(),
      const QString& destination_directory = QString(),
      const QVariantMap& metrics = QVariantMap());
  Q_INVOKABLE QVariantMap queueExportForSelected(
      const QString& preset = QString(),
      const QString& destination_directory = QString(),
      bool confirmed = false,
      const QVariantMap& metrics = QVariantMap());
  Q_INVOKABLE QVariantMap planDraftForSelected(const QString& instructions = QString()) const;
  Q_INVOKABLE QVariantMap planDraftForScope(
      const QString& scope,
      const QString& instructions = QString(),
      const QVariantMap& mcp_context = QVariantMap()) const;
  Q_INVOKABLE QVariantMap runDraftOnSelected(const QString& instructions = QString());
  Q_INVOKABLE QVariantMap runDraftForScope(
      const QString& scope,
      const QString& instructions = QString());
  Q_INVOKABLE QVariantMap startDraftOnSelected(
      const QString& instructions = QString());
  // Explicit targets remain independent of library focus/filter changes, including review.
  Q_INVOKABLE QVariantMap startDraftForConfirmedAssets(
      const QStringList& asset_ids, const QString& instructions);
  [[nodiscard]] QVariantMap startDraftForPreparedTask(
      const QStringList& asset_ids,
      const QString& instructions,
      const QString& prepared_task_id);
  Q_INVOKABLE QVariantMap startDraftForScope(
      const QString& scope,
      const QString& instructions = QString());
  Q_INVOKABLE QVariantMap applyEditForSelected(const QString& instructions = QString());
  Q_INVOKABLE QVariantMap applyEditForScope(
      const QString& scope,
      const QString& instructions = QString());
  Q_INVOKABLE QVariantMap answerPendingUserInput(const QString& answer);
  Q_INVOKABLE QVariantMap confirmGroundedSelectionCandidate(
      const QString& candidate_id);
  Q_INVOKABLE QVariantMap dismissPendingAgentInput();
  Q_INVOKABLE QVariantMap applyDraftRecipePatchToSelected(const QVariantMap& patch);
  Q_INVOKABLE QVariantMap applyDraftRecipePatchForScope(
      const QString& scope,
      const QVariantMap& patch);
  Q_INVOKABLE QVariantMap applySampleReviewToAll(bool confirmed = false);
  Q_INVOKABLE QVariantMap compareDraftDirection(
      const QString& alternative_direction);
  Q_INVOKABLE QVariantMap compareDraftWithPhotographerCalibration();
  Q_INVOKABLE QVariantMap chooseDraftDirection(const QString& direction_key);
  Q_INVOKABLE QVariantMap reviseDraft(const QString& feedback);
  Q_INVOKABLE QVariantMap reviseDraftFinding(
      const QVariantMap& finding,
      const QString& feedback);
  Q_INVOKABLE QVariantMap reviseDraftAtPoint(
      double normalized_x,
      double normalized_y,
      const QString& feedback,
      const QString& asset_id = QString());
  Q_INVOKABLE QVariantMap prepareRejectedRetouchRepairAtPoint(
      double normalized_x,
      double normalized_y,
      const QString& asset_id = QString());
  Q_INVOKABLE QVariantMap setDraftFindingProtected(
      const QVariantMap& finding,
      bool protected_intent);
  Q_INVOKABLE QVariantMap reviseSampleReview(const QString& feedback);
  Q_INVOKABLE QVariantMap acceptReviewIssue(const QString& asset_id);
  Q_INVOKABLE QVariantMap fixReviewIssue(const QString& asset_id);
  Q_INVOKABLE QVariantMap fixAllReviewIssues();
  Q_INVOKABLE QVariantMap rollbackReviewIssue(const QString& asset_id);
  Q_INVOKABLE QVariantMap keepValidatedFastDraft();
  Q_INVOKABLE QVariantMap chooseTwoSpeedDraftVersion(
      const QString& version_key);
  Q_INVOKABLE QVariantMap cancelRun(const QString& reason = QString());
  Q_INVOKABLE QVariantMap resumeLastFailedBatchDraft();
  Q_INVOKABLE QVariantMap rollbackDraft(const QString& reason = QString());
  Q_INVOKABLE QVariantMap commitDraft(bool confirmed = false);

 signals:
  void managedRuntimeStartRequested();
  void managedModelInstallStateChanged(const QString& model, bool installing);
  void statusChanged();
  void errorMessageChanged();
  void runningChanged();
  void cancellationChanged();
  void taskTransactionChanged();
  void manualEditingActiveChanged();
  void runTimeoutMsChanged();
  void draftChanged();
  void selectedStyleChanged();
  void selectedModelChanged();
  void userInstructionsChanged();
  void recipeBaseModeChanged();
  void editingFreedomChanged();
  void availableStylesChanged();
  void availableModelsChanged();
  void modelProbeChanged();
  void mcpStatusChanged();
  void modelInstallChanged();
  void lastRunReportChanged();
  void progressivePreviewChanged();
  void draftRequestChanged();
  void draftRunFinished(const QString& request_id, const QVariantMap& report);
  void runEventsChanged();
  void runHistoryChanged();
  void projectTasteChanged();
  void pendingAgentInputChanged();

 private:
  std::function<bool()> access_policy_;
  QVariantList scopedAssetMaps(const QString& scope) const;
  [[nodiscard]] QVariantMap planningRecipeSnapshot(const QVariantMap& asset) const;
  [[nodiscard]] bool takeOrStartPlanningProof(
      const QString& stage, const QVariantMap& asset, const QVariantMap& style,
      const QString& run_id, const QString& scope, bool validate_live, QVariantMap* report,
      const QVariantMap& overlay_patch = {});
  QStringList confirmed_asset_ids_;
  QVariantMap finalizePhotographerCalibrationReviewPayload(
      const QByteArray& ratings_bytes,
      const QString& packet_path_or_url);
  QVariantMap assignmentStoryMapForAssets(
      const QVariantList& assets,
      const QVariantMap& cluster_metadata) const;
  QVariantMap assignmentStoryRoleOverridesForAssets(
      const QVariantList& assets) const;
  QVariantMap rememberedAssignmentStoryPreference() const;
  void continueDraftRequest(
      const QString& request_id,
      const QString& scope,
      const QString& instructions);
  [[nodiscard]] bool takeOrStartRequestOwnedPlanStage(
      const QString& stage_name,
      const AgentLocalModelPlanRequest& request,
      const QString& scope,
      const QString& instructions,
      AgentLocalModelPlanResult* result);
  [[nodiscard]] QVariantMap requestOwnedPlanningContinuationReport() const;
  [[nodiscard]] QVariantMap requestOwnedPlanPendingReport(
      const QString& stage_name,
      const QString& scope) const;
  [[nodiscard]] bool takeOrStartRequestOwnedWorkerStage(
      const QString& stage_name,
      const QString& scope,
      const std::function<QVariantMap(
          const std::shared_ptr<std::atomic_bool>&)>& worker,
      QVariantMap* result);
  [[nodiscard]] QVariantMap requestOwnedWorkerContinuationReport() const;
  [[nodiscard]] QVariantMap requestOwnedWorkerPendingReport(
      const QString& stage_name,
      const QString& scope) const;
  [[nodiscard]] QVariantMap beginTaskTransaction(const QString& request_id, const QStringList& asset_ids);
  [[nodiscard]] int applyTaskOwnedAdjustmentPatchJson(
      const QStringList& asset_ids,
      const QString& patch_json,
      const QStringList& adjustment_groups = {});
  [[nodiscard]] bool finalizeTaskTransaction(
      const QString& request_id,
      QVariantMap* report,
      bool emit_completion = false);
  [[nodiscard]] bool retainPendingTaskFinalization(
      const QString& task_id,
      const QVariantMap& receipt,
      const QString& request_id,
      const QString& action,
      const QVariantMap& report,
      bool emit_completion,
      bool clear_draft_on_success = false);
  void recordAcceptedCompositionPhotoRevision(const QVariantMap& report);
  [[nodiscard]] QVariantMap enqueueDraftRequest(const QString& scope, const QString& instructions, const QString& request_id);
  void attachRevisionOutcome(QVariantMap* report);
  [[nodiscard]] QVariantMap executeDraftForScope(const QString& scope, const QString& instructions);
  void clearAsyncModelPlanningState();
  void scheduleCompositionContinuation(
      const QString& request_id, const QString& scope,
      const QString& instructions, const QString& tool_plan_key);
  [[nodiscard]] QVariantMap finalizeDraftDirectionComparison(
      const QString& request_id,
      const QVariantMap& completed_report);
  [[nodiscard]] QVariantMap compareDraftDirectionWithContext(
      const QString& alternative_direction,
      const QVariantMap& photographer_taste_check);
  [[nodiscard]] QVariantMap restoreDraftDirectionA(
      const QString& reason);
  void clearDraftDirectionComparison();
  void setStatus(const QString& status);
  void setErrorMessage(const QString& error_message);
  void setRunning(bool running);
  void setLastRunReport(const QVariantMap& report);
  void setProgressivePreview(const QVariantMap& preview);
  [[nodiscard]] QVariantMap publishFastPlanningPreview(
      const QString& scope,
      const QString& instructions,
      const QVariantMap& style,
      const QVariantMap& asset);
  [[nodiscard]] QVariantMap restoreAsyncPlanningPreview(
      const QString& state,
      const QString& reason);
  void startFastPlanningPreviewValidation(
      const QString& request_id,
      const QString& instructions,
      const QVariantMap& style,
      const QVariantMap& asset,
      const EditRecipeState& original_state,
      const EditRecipeState& fast_state,
      const QVariantMap& fast_recipe_patch);
  [[nodiscard]] QVariantMap rejectFastPlanningPreview(
      const QString& reason,
      const QVariantMap& validation_report = QVariantMap());
  void beginRunEvents(const QString& scope);
  void attachRunInteractionFeedback(QVariantMap* report) const;
  [[nodiscard]] bool batchRunStopRequested(
      const QElapsedTimer& run_timer,
      QString* cancellation_reason,
      QStringList* errors,
      QVariantList* tool_calls);
  void appendRunEvent(
      const QString& stage,
      const QString& label,
      const QString& status,
      const QString& detail = QString(),
      const QVariantMap& evidence = QVariantMap());
  void setMcpStatusState(
      const QString& status,
      const QStringList& tool_names,
      bool available,
      const QString& last_error = QString());
  void setMcpTelemetryState(
      int connected_client_count,
      int total_client_count,
      const QString& last_tool_call_name,
      const QString& last_tool_call_status,
      const QString& last_tool_call_at);
  void setModelInstallState(
      bool running,
      const QString& status,
      const QString& output,
      const QVariantMap& progress = QVariantMap());
  void setPendingAgentInput(const QVariantMap& input);
  void clearPendingAgentInput();
  void refreshMemoryVaultSnapshot() const;
  void applyModelProbeReport(const QVariantMap& report);
  [[nodiscard]] QString currentContinuityProjectKey() const;
  [[nodiscard]] QVariantList recentVisualReferents() const;
  [[nodiscard]] QVariantMap persistConfirmedVisualReferent(
      const QString& asset_id,
      const QString& target_label,
      const QString& mask_id,
      const QVariantMap& candidate,
      const QVariantMap& confirmed_run_report);
  void restoreAgentContinuity();
  void persistAgentContinuity(const QString& project_key_override = {});
  [[nodiscard]] QVariantMap persistApprovedProjectTaste(
      const QVariantMap& commit_report);
  [[nodiscard]] QVariantMap persistRejectedProjectDraft(
      const QVariantMap& rollback_report);
  [[nodiscard]] QVariantMap ensureMcpReady(
      const QString& reason,
      int max_attempts = 3);
  [[nodiscard]] QVariantMap renderCurrentProofForSelected(
      const QString& mode,
      const QString& start_status,
      const QString& success_status,
      const QString& summary,
      const QString& event_name);
  [[nodiscard]] QVariantMap applyDraftRecipePatchesForScope(
      const QString& scope,
      const QVariantMap& default_patch,
      const QHash<QString, QVariantMap>& patches_by_asset_id,
      const QVariantMap& batch_plan_metadata = QVariantMap());
  [[nodiscard]] QVariantMap createLearnedStyleProfileFromAssetMaps(
      const QString& name,
      const QString& goal,
      const QString& notes,
      const QVariantList& assets);
  [[nodiscard]] QVariantMap selectedStyle() const;
  [[nodiscard]] QVariantMap activeRunStyle() const;
  [[nodiscard]] QString styleDirectory() const;

  CatalogAssetModel* catalog_model_ = nullptr;
  QMetaObject::Connection catalog_filters_connection_;
  ExportJobController* export_job_controller_ = nullptr;
  QString status_ = QStringLiteral("Idle");
  QString error_message_;
  bool running_ = false;
  std::atomic_bool cancellation_requested_ = false;
  bool rollback_on_cancel_ = false;
  bool manual_editing_active_ = false;
  QString cancellation_reason_;
  int run_timeout_ms_ = -1;
  QString selected_style_id_;
  QString selected_model_id_;
  QString user_instructions_;
  QString recipe_base_mode_ = QStringLiteral("current");
  QString editing_freedom_ = QStringLiteral("balanced");
  QVariantList available_styles_;
  QVariantList available_models_;
  QString mcp_endpoint_;
  QString mcp_endpoint_kind_;
  QString mcp_bearer_token_;
  QString mcp_status_ = QStringLiteral("Unavailable");
  QString mcp_last_error_;
  QStringList mcp_tool_names_;
  QVariantList mcp_tools_;
  QString mcp_tool_schema_fingerprint_;
  int mcp_tool_schema_revision_ = 0;
  bool mcp_tool_schema_changed_ = false;
  QStringList mcp_added_tool_names_;
  QStringList mcp_removed_tool_names_;
  bool mcp_available_ = false;
  int mcp_connected_client_count_ = 0;
  int mcp_total_client_count_ = 0;
  QString mcp_last_tool_call_name_;
  QString mcp_last_tool_call_status_;
  QString mcp_last_tool_call_at_;
  QProcess* model_install_process_ = nullptr;
  ManagedAgentModelDownload* managed_model_download_ = nullptr;
  QString model_install_status_ = QStringLiteral("Idle");
  QString model_install_output_;
  QVariantMap model_install_progress_;
  bool model_probe_running_ = false;
  QString model_probe_model_id_;
  QVariantMap last_run_report_;
  QVariantMap progressive_preview_;
  QString active_draft_request_id_;
  QString active_request_task_id_;
  QVariantMap async_revision_context_;
  QString task_transaction_id_;
  QVariantMap task_transaction_state_;
  // Host-owned no-text photographic baseline retained only for the active
  // reversible task and its explicitly approved revisions.
  QVariantMap task_composition_photo_baseline_;
  QVariantMap pending_task_finalization_;
  QVariantMap approved_task_snapshot_;
  QVariantList approved_task_snapshot_history_;
  QHash<QString, QString> task_original_adjustment_patches_;
  QVariantMap task_expected_adjustment_patches_;
  std::function<QVariantMap(const QString&, const QString&, const QStringList&, const QVariantMap&)> task_transaction_handler_;
  std::function<QVariantMap(const QString&)> planning_snapshot_handler_;
  bool draft_request_pending_ = false;
  bool allow_queued_draft_entry_once_ = false;
  bool async_model_planning_enabled_ = false;
  bool async_model_planning_in_flight_ = false;
  bool async_request_worker_in_flight_ = false;
  bool async_model_continuation_resume_pending_ = false;
  int async_model_stage_dispatch_count_ = 0;
  int async_request_worker_stage_dispatch_count_ = 0;
  QString async_model_plan_fingerprint_;
  QString async_model_active_stage_key_;
  QString async_request_worker_active_stage_key_;
  QString async_draft_project_key_;
  QString async_draft_style_id_;
  QString async_draft_model_id_;
  QString async_draft_scope_;
  QString async_draft_instructions_;
  QStringList async_draft_asset_ids_;
  QString async_mcp_context_request_id_;
  QString async_mcp_context_key_;
  QVariantMap async_mcp_discovery_;
  QVariantMap async_mcp_observation_;
  QHash<QString, std::shared_ptr<AgentLocalModelPlanResult>>
      async_model_stage_results_;
  QHash<QString, QVariantMap> async_model_tool_plan_results_;
  QHash<QString, QVariantMap> async_model_round_planning_proofs_;
  QHash<QString, int> async_composition_repair_round_cursors_;
  QHash<QString, QVariantMap> async_composition_repair_progress_snapshots_;
  QHash<QString, QStringList> async_composition_repair_plan_signatures_;
  QHash<QString, QVariantMap> async_composition_repair_accepted_recipe_results_;
  bool async_composition_resume_scheduled_ = false;
  QHash<QString, std::shared_ptr<AgentLocalModelProofCritiqueResult>>
      async_model_proof_critique_results_;
  QStringList async_model_completed_stages_;
  QHash<QString, QVariantMap> async_request_worker_stage_results_;
  QStringList async_request_worker_completed_stages_;
  QHash<QString, QString> async_progressive_preview_originals_;
  QHash<QString, QString> async_progressive_preview_authored_;
  bool async_fast_preview_validation_in_flight_ = false;
  QString async_fast_preview_validation_request_id_;
  QVariantMap async_fast_preview_validation_report_;
  QVariantMap async_fast_preview_recipe_patch_;
  QString async_fast_preview_adjustment_patch_json_;
  std::shared_ptr<std::atomic_bool> async_fast_preview_validation_token_;
  std::shared_ptr<std::atomic_bool> async_model_cancellation_token_;
  QElapsedTimer async_draft_run_timer_;
  QDateTime async_draft_started_at_utc_;
  QVariantList run_events_;
  QElapsedTimer run_feedback_timer_;
  QVariantList run_history_;
  bool restoring_continuity_ = false;
  QString restored_continuity_project_key_;
  QVariantMap pending_agent_input_;
  mutable bool memory_vault_snapshot_initialized_ = false;
  mutable QVariantMap memory_vault_snapshot_;
  bool suppress_memory_writes_ = false;
  QHash<QString, QVariantMap> transient_visual_referent_masks_;
  QHash<QString, QString>
      draft_original_adjustment_patches_;
  QString draft_run_id_;
  QString draft_project_key_;
  QVariantMap draft_direction_comparison_;
  QVariantMap assignment_story_role_overrides_;
  QString assignment_story_override_project_key_;
  QStringList assignment_story_override_asset_ids_;
  QVariantMap draft_direction_a_report_;
  QHash<QString, QString> draft_direction_a_adjustment_patches_;
  QHash<QString, QString> draft_direction_original_adjustment_patches_;
  QString draft_direction_a_run_id_;
  QString draft_direction_project_key_;
  QString pending_direction_comparison_request_id_;
  QString pending_direction_alternative_;
};

}  // namespace photo_editor::desktop
