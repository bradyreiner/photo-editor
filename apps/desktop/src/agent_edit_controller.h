#pragma once

#include <QObject>
#include <QHash>
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>

#include "agent_edit_style.h"

namespace photo_editor::desktop {

class CatalogAssetModel;
class ExportJobController;

class AgentEditController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(bool cancellationRequested READ cancellationRequested NOTIFY cancellationChanged)
  Q_PROPERTY(bool manualEditingActive READ manualEditingActive WRITE setManualEditingActive NOTIFY manualEditingActiveChanged)
  Q_PROPERTY(int runTimeoutMs READ runTimeoutMs WRITE setRunTimeoutMs NOTIFY runTimeoutMsChanged)
  Q_PROPERTY(bool draftActive READ draftActive NOTIFY draftChanged)
  Q_PROPERTY(QString selectedStyleId READ selectedStyleId WRITE setSelectedStyleId NOTIFY selectedStyleChanged)
  Q_PROPERTY(QString selectedModelId READ selectedModelId WRITE setSelectedModelId NOTIFY selectedModelChanged)
  Q_PROPERTY(QString userInstructions READ userInstructions WRITE setUserInstructions NOTIFY userInstructionsChanged)
  Q_PROPERTY(QString recipeBaseMode READ recipeBaseMode WRITE setRecipeBaseMode NOTIFY recipeBaseModeChanged)
  Q_PROPERTY(QVariantList availableStyles READ availableStyles NOTIFY availableStylesChanged)
  Q_PROPERTY(QVariantList availableModels READ availableModels NOTIFY availableModelsChanged)
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
  Q_PROPERTY(QVariantList runHistory READ runHistory NOTIFY runHistoryChanged)
  Q_PROPERTY(bool awaitingUserInput READ awaitingUserInput NOTIFY pendingAgentInputChanged)
  Q_PROPERTY(QVariantList pendingUserQuestions READ pendingUserQuestions NOTIFY pendingAgentInputChanged)
  Q_PROPERTY(QString safeStopReason READ safeStopReason NOTIFY pendingAgentInputChanged)

 public:
  explicit AgentEditController(QObject* parent = nullptr);

  void setCatalogModel(CatalogAssetModel* catalog_model);
  void setExportJobController(ExportJobController* export_job_controller);

  [[nodiscard]] QString status() const;
  [[nodiscard]] QString errorMessage() const;
  [[nodiscard]] bool running() const;
  [[nodiscard]] bool cancellationRequested() const;
  [[nodiscard]] bool manualEditingActive() const;
  [[nodiscard]] int runTimeoutMs() const;
  [[nodiscard]] bool draftActive() const;
  [[nodiscard]] QString selectedStyleId() const;
  [[nodiscard]] QString selectedModelId() const;
  [[nodiscard]] QString userInstructions() const;
  [[nodiscard]] QString recipeBaseMode() const;
  [[nodiscard]] QVariantList availableStyles() const;
  [[nodiscard]] QVariantList availableModels() const;
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
  [[nodiscard]] QVariantList runHistory() const;
  [[nodiscard]] bool awaitingUserInput() const;
  [[nodiscard]] QVariantList pendingUserQuestions() const;
  [[nodiscard]] QString safeStopReason() const;

  void setSelectedStyleId(const QString& style_id);
  void setSelectedModelId(const QString& model_id);
  void setUserInstructions(const QString& instructions);
  void setRecipeBaseMode(const QString& mode);
  void setRunTimeoutMs(int timeout_ms);
  void setManualEditingActive(bool active);
  void setMcpEndpoint(const QString& endpoint, const QString& endpoint_kind);

  Q_INVOKABLE void reloadStyles();
  Q_INVOKABLE void reloadModels();
  Q_INVOKABLE QVariantMap refreshMcpTools();
  Q_INVOKABLE QVariantMap prepareSelectedModelInstall() const;
  Q_INVOKABLE QVariantMap setupLocalAi(bool confirmed = false);
  Q_INVOKABLE QVariantMap installSelectedModel(bool confirmed = false);
  Q_INVOKABLE QVariantMap probeSelectedModel();
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
  Q_INVOKABLE QVariantMap createLearnedStyleProfileFromSelection(
      const QString& name,
      const QString& goal,
      const QString& notes = QString());
  Q_INVOKABLE QVariantMap planStyleProjectRun(const QString& scope = QStringLiteral("selected-assets"));
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
  Q_INVOKABLE QVariantMap aiEditWorkflowState(
      const QString& scope = QString()) const;
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
  Q_INVOKABLE QVariantMap applyEditForSelected(const QString& instructions = QString());
  Q_INVOKABLE QVariantMap applyEditForScope(
      const QString& scope,
      const QString& instructions = QString());
  Q_INVOKABLE QVariantMap answerPendingUserInput(const QString& answer);
  Q_INVOKABLE QVariantMap dismissPendingAgentInput();
  Q_INVOKABLE QVariantMap applyDraftRecipePatchToSelected(const QVariantMap& patch);
  Q_INVOKABLE QVariantMap applyDraftRecipePatchForScope(
      const QString& scope,
      const QVariantMap& patch);
  Q_INVOKABLE QVariantMap applySampleReviewToAll(bool confirmed = false);
  Q_INVOKABLE QVariantMap reviseSampleReview(const QString& feedback);
  Q_INVOKABLE QVariantMap acceptReviewIssue(const QString& asset_id);
  Q_INVOKABLE QVariantMap fixReviewIssue(const QString& asset_id);
  Q_INVOKABLE QVariantMap fixAllReviewIssues();
  Q_INVOKABLE QVariantMap rollbackReviewIssue(const QString& asset_id);
  Q_INVOKABLE QVariantMap cancelRun(const QString& reason = QString());
  Q_INVOKABLE QVariantMap resumeLastFailedBatchDraft();
  Q_INVOKABLE QVariantMap rollbackDraft();
  Q_INVOKABLE QVariantMap commitDraft(bool confirmed = false);

 signals:
  void statusChanged();
  void errorMessageChanged();
  void runningChanged();
  void cancellationChanged();
  void manualEditingActiveChanged();
  void runTimeoutMsChanged();
  void draftChanged();
  void selectedStyleChanged();
  void selectedModelChanged();
  void userInstructionsChanged();
  void recipeBaseModeChanged();
  void availableStylesChanged();
  void availableModelsChanged();
  void mcpStatusChanged();
  void modelInstallChanged();
  void lastRunReportChanged();
  void runHistoryChanged();
  void pendingAgentInputChanged();

 private:
  void setStatus(const QString& status);
  void setErrorMessage(const QString& error_message);
  void setRunning(bool running);
  void setLastRunReport(const QVariantMap& report);
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
  [[nodiscard]] QVariantMap selectedStyle() const;
  [[nodiscard]] QString styleDirectory() const;

  CatalogAssetModel* catalog_model_ = nullptr;
  ExportJobController* export_job_controller_ = nullptr;
  QString status_ = QStringLiteral("Idle");
  QString error_message_;
  bool running_ = false;
  bool cancellation_requested_ = false;
  bool rollback_on_cancel_ = false;
  bool manual_editing_active_ = false;
  QString cancellation_reason_;
  int run_timeout_ms_ = 300000;
  QString selected_style_id_;
  QString selected_model_id_;
  QString user_instructions_;
  QString recipe_base_mode_ = QStringLiteral("current");
  QVariantList available_styles_;
  QVariantList available_models_;
  QString mcp_endpoint_;
  QString mcp_endpoint_kind_;
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
  QString model_install_status_ = QStringLiteral("Idle");
  QString model_install_output_;
  QVariantMap model_install_progress_;
  QVariantMap last_run_report_;
  QVariantList run_history_;
  QVariantMap pending_agent_input_;
  mutable bool memory_vault_snapshot_initialized_ = false;
  mutable QVariantMap memory_vault_snapshot_;
  QHash<QString, QString> draft_original_recipes_;
  QString draft_run_id_;
};

}  // namespace photo_editor::desktop
