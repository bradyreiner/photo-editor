#pragma once

#include <QObject>
#include <QHash>
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
  Q_PROPERTY(QVariantMap lastRunReport READ lastRunReport NOTIFY lastRunReportChanged)
  Q_PROPERTY(QVariantList runHistory READ runHistory NOTIFY runHistoryChanged)

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
  [[nodiscard]] QVariantMap lastRunReport() const;
  [[nodiscard]] QVariantList runHistory() const;

  void setSelectedStyleId(const QString& style_id);
  void setSelectedModelId(const QString& model_id);
  void setUserInstructions(const QString& instructions);
  void setRecipeBaseMode(const QString& mode);
  void setRunTimeoutMs(int timeout_ms);
  void setManualEditingActive(bool active);

  Q_INVOKABLE void reloadStyles();
  Q_INVOKABLE QVariantMap validateStyleJson(const QString& style_json) const;
  Q_INVOKABLE bool saveStyle(const QVariantMap& style);
  Q_INVOKABLE QVariantMap exportStyle(
      const QString& style_id = QString(),
      const QString& destination_directory = QString());
  Q_INVOKABLE QVariantMap importStyleJson(
      const QString& style_json,
      const QString& name_override = QString());
  Q_INVOKABLE QVariantMap createStyle(
      const QString& name,
      const QString& goal,
      const QVariantMap& allowed_tools,
      const QVariantMap& constraints,
      const QVariantMap& validation,
      const QVariantMap& batch_behavior);
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
      const QString& instructions = QString()) const;
  Q_INVOKABLE QVariantMap runDraftOnSelected(const QString& instructions = QString());
  Q_INVOKABLE QVariantMap runDraftForScope(
      const QString& scope,
      const QString& instructions = QString());
  Q_INVOKABLE QVariantMap applyDraftRecipePatchToSelected(const QVariantMap& patch);
  Q_INVOKABLE QVariantMap applyDraftRecipePatchForScope(
      const QString& scope,
      const QVariantMap& patch);
  Q_INVOKABLE QVariantMap cancelRun(const QString& reason = QString());
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
  void lastRunReportChanged();
  void runHistoryChanged();

 private:
  void setStatus(const QString& status);
  void setErrorMessage(const QString& error_message);
  void setRunning(bool running);
  void setLastRunReport(const QVariantMap& report);
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
  QVariantMap last_run_report_;
  QVariantList run_history_;
  QHash<QString, QString> draft_original_recipes_;
  QString draft_run_id_;
};

}  // namespace photo_editor::desktop
