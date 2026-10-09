#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "agent_local_model.h"

namespace photo_editor::desktop {

class AgentSessionController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap brief READ brief NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap plan READ plan NOTIFY stateChanged)
  Q_PROPERTY(QVariantList questions READ questions NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  Q_PROPERTY(QString activity READ activity NOTIFY activityChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(bool canStart READ canStart NOTIFY stateChanged)
  Q_PROPERTY(bool canRevert READ canRevert NOTIFY stateChanged)
  Q_PROPERTY(bool revertPending READ revertPending NOTIFY stateChanged)
  Q_PROPERTY(QString revertOwner READ revertOwner NOTIFY stateChanged)

 public:
  using PlanningHandler = std::function<AgentLocalModelTaskSessionResult(
      const AgentLocalModelTaskSessionRequest&)>;
  using ContextSnapshotHandler = std::function<QVariantMap(const QVariantMap&)>;
  using ExecutionControlHandler = std::function<QVariantMap(const QString&)>;
  using RevertAvailabilityHandler = std::function<QVariantMap()>;

  explicit AgentSessionController(QObject* parent = nullptr);
  // The handler overload is a deterministic seam for native tests.
  explicit AgentSessionController(PlanningHandler test_handler, QObject* parent = nullptr);
  ~AgentSessionController() override;

  [[nodiscard]] QString stage() const;
  [[nodiscard]] QVariantMap brief() const;
  [[nodiscard]] QVariantMap plan() const;
  [[nodiscard]] QVariantList questions() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QString activity() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] bool canStart() const;
  [[nodiscard]] bool canRevert() const;
  [[nodiscard]] bool revertPending() const;
  [[nodiscard]] QString revertOwner() const;

  Q_INVOKABLE void submit(const QString& prompt, const QVariantMap& context);
  Q_INVOKABLE void answerQuestions(const QVariantMap& answers);
  Q_INVOKABLE void reviseAnswers(const QVariantMap& answers);
  Q_INVOKABLE void planWithDefaults(const QVariantMap& answers = {});
  Q_INVOKABLE void updateContext(const QVariantMap& context);
  Q_INVOKABLE void retryPlanning();
  Q_INVOKABLE void start();
  Q_INVOKABLE QVariantMap validateApprovedExecutionSnapshot(const QVariantMap& snapshot) const;
  Q_INVOKABLE void cancelPlanning();
  Q_INVOKABLE void reset();
  Q_INVOKABLE void acknowledgeExecution(const QVariantMap& report);
  Q_INVOKABLE QVariantMap requestStop();
  Q_INVOKABLE QVariantMap requestRevert();
  void setContextSnapshotHandler(ContextSnapshotHandler handler);
  void setExecutionControlHandler(ExecutionControlHandler handler);
  void setRevertAvailabilityHandler(RevertAvailabilityHandler handler);
  void notifyRevertAvailabilityChanged();
  [[nodiscard]] QVariantMap resolvePreparedCanvasTarget(const QString& task_id, const QString& asset_id);
  void reportExecutionActivity(const QString& activity);

 signals:
  void stateChanged();
  void activityChanged();
  void activityReceived(const QString& activity);
  void executionRequested(const QVariantMap& approvedSnapshot);
  void preparedTargetResolved(const QString& asset_id);

 private:
  void launchPlanning(QVariantMap plan_validation_feedback = {});
  void invalidatePlanning();
  void updateBriefFromContext();
  void setStage(const QString& stage, const QString& status);
  [[nodiscard]] QVariantMap currentTargetSnapshot() const;
  [[nodiscard]] QVariantMap normalizeContext(const QVariantMap& context) const;
  [[nodiscard]] QString contextReadinessError() const;
  [[nodiscard]] QString selectedModelId() const;
  [[nodiscard]] QVariantMap normalizeContextSnapshot(const QVariantMap& context) const;
  [[nodiscard]] QVariantMap revertAvailability() const;

  PlanningHandler planning_handler_;
  ContextSnapshotHandler context_snapshot_handler_;
  ExecutionControlHandler execution_control_handler_;
  RevertAvailabilityHandler revert_availability_handler_;
  bool test_handler_injected_ = false;
  QString stage_ = QStringLiteral("idle");
  QVariantMap brief_;
  QVariantMap raw_context_;
  QVariantMap context_;
  QVariantMap plan_;
  QVariantMap last_approved_snapshot_;
  QVariantMap task_origin_snapshot_;
  QVariantMap last_execution_report_;
  bool revert_request_pending_ = false;
  bool revision_session_ = false;
  QVariantList questions_;
  QVariantList prior_questions_;
  QString status_;
  QString activity_;
  bool busy_ = false;
  quint64 brief_version_ = 0;
  quint64 request_id_ = 0;
  std::shared_ptr<std::atomic_bool> cancellation_token_;
  QObject* active_watcher_ = nullptr;
};

}  // namespace photo_editor::desktop
