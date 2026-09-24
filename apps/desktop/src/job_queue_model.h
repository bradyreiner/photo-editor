#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QVector>
#include <functional>

#include "job_store.h"

namespace photo_editor::desktop {

struct JobHistoryReadResult final {
  QVector<PersistentJobRecord> records;
  QString error;
};

class JobQueueModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int jobCount READ jobCount NOTIFY jobsChanged)
  Q_PROPERTY(int activeJobCount READ activeJobCount NOTIFY jobsChanged)
  Q_PROPERTY(int failedJobCount READ failedJobCount NOTIFY jobsChanged)
  Q_PROPERTY(int completedJobCount READ completedJobCount NOTIFY jobsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY jobsChanged)
  Q_PROPERTY(bool hasFailures READ hasFailures NOTIFY jobsChanged)
  Q_PROPERTY(bool hasRecoveryReport READ hasRecoveryReport NOTIFY jobsChanged)
  Q_PROPERTY(int recoveredJobCount READ recoveredJobCount NOTIFY jobsChanged)
  Q_PROPERTY(QString recoverySummary READ recoverySummary NOTIFY jobsChanged)
  Q_PROPERTY(QStringList recoveryDetails READ recoveryDetails NOTIFY jobsChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(QString historyError READ historyError NOTIFY loadingChanged)
  Q_PROPERTY(QString startupRecoveryError READ startupRecoveryError NOTIFY startupRecoveryErrorChanged)
  Q_PROPERTY(bool historyMutationsEnabled READ historyMutationsEnabled NOTIFY startupRecoveryErrorChanged)

 public:
  enum Roles {
    JobIdRole = Qt::UserRole + 1,
    JobKindRole,
    AssetIdRole,
    MediaTypeRole,
    DisplayLabelRole,
    SourcePathRole,
    PresetNameRole,
    DestinationPathRole,
    OutputPathRole,
    StatusRole,
    ErrorMessageRole,
    ProgressRole,
    CreatedAtRole,
    UpdatedAtRole,
    MetadataRole,
    StageRole,
    PriorityRole,
    CanPauseRole,
    CanResumeRole,
    CanCancelRole,
    CanRetryRole,
    AffectedTypeRole,
    AffectedIdRole,
    JumpLabelRole,
    QueueOrderRole,
    CanMoveUpRole,
    CanMoveDownRole,
    CanDuplicateRole,
    CanRevealOutputRole,
  };

  explicit JobQueueModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] int jobCount() const;
  [[nodiscard]] int activeJobCount() const;
  [[nodiscard]] int failedJobCount() const;
  [[nodiscard]] int completedJobCount() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] bool hasFailures() const;
  [[nodiscard]] bool hasRecoveryReport() const;
  [[nodiscard]] int recoveredJobCount() const;
  [[nodiscard]] QString recoverySummary() const;
  [[nodiscard]] QStringList recoveryDetails() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] bool loading() const { return history_loading_; }
  [[nodiscard]] QString historyError() const { return history_error_; }
  [[nodiscard]] QString startupRecoveryError() const { return startup_recovery_error_; }
  [[nodiscard]] bool historyMutationsEnabled() const { return startup_recovery_error_.isEmpty(); }
  void setStartupRecoveryError(const QString& error);

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void initializeFromStore();
  Q_INVOKABLE void acknowledgeRecoveryReport();
  Q_INVOKABLE int activeJobCountForKind(const QString& job_kind) const;
  Q_INVOKABLE int failedJobCountForKind(const QString& job_kind) const;
  Q_INVOKABLE bool hasCurrentFailureForKind(const QString& job_kind) const;
  Q_INVOKABLE int totalJobCountForKind(const QString& job_kind) const;
  Q_INVOKABLE QVariantMap progressSummaryForKind(const QString& job_kind) const;
  Q_INVOKABLE QVariantMap latestJobForKind(const QString& job_kind) const;
  Q_INVOKABLE QVariantMap latestFailedJobForKind(const QString& job_kind) const;
  Q_INVOKABLE QVariantList jobsForCenter(const QString& job_kind = QString()) const;
  Q_INVOKABLE QVariantMap affectedItemForJob(const QString& job_id) const;
  Q_INVOKABLE bool setJobPriority(const QString& job_id, const QString& priority);
  Q_INVOKABLE bool setRenderResourcePolicy(
      const QString& job_id,
      const QString& policy);
  Q_INVOKABLE bool moveRenderJob(const QString& job_id, int direction);
  Q_INVOKABLE bool canMoveRenderJob(const QString& job_id, int direction) const;
  Q_INVOKABLE QString duplicateRenderJob(const QString& job_id);
  Q_INVOKABLE bool canDuplicateRenderJob(const QString& job_id) const;
  Q_INVOKABLE QString revealOutputPath(const QString& job_id) const;
  Q_INVOKABLE bool revealJobOutput(const QString& job_id) const;
  Q_INVOKABLE QString statusForKind(
      const QString& job_kind,
      const QString& idle_status = QString()) const;
  Q_INVOKABLE bool canCancelJob(const QString& job_id) const;
  Q_INVOKABLE QString cancellationPolicySignature(
      const QString& job_kind,
      const QString& status) const;
  Q_INVOKABLE QString cancellationSignature(const QString& job_id) const;
  Q_INVOKABLE bool retryJob(const QString& job_id);
  Q_INVOKABLE bool cancelJob(const QString& job_id);
  Q_INVOKABLE bool canPauseJob(const QString& job_id) const;
  Q_INVOKABLE bool canResumeJob(const QString& job_id) const;
  Q_INVOKABLE bool pauseJob(const QString& job_id);
  Q_INVOKABLE bool resumeJob(const QString& job_id);
  Q_INVOKABLE void retryFailedJobs();
  Q_INVOKABLE void removeJob(const QString& job_id);
  Q_INVOKABLE void clearFinishedJobs();

 signals:
  void jobsChanged();
  void statusChanged();
  void loadingChanged();
  void startupRecoveryErrorChanged();

 private:
  friend struct JobQueueModelTestAccess;
  void reloadJobs();
  void publishJobs(const QVector<PersistentJobRecord>& records);
  void requestHistoryLoad(bool recover_interrupted);
  void startHistoryLoad();
  void setStatus(const QString& status);
  [[nodiscard]] static QString summaryStatusForJobSubset(
      const QVector<PersistentJobRecord>& jobs,
      const QString& idle_status);
  [[nodiscard]] int activeJobCountValue() const;
  [[nodiscard]] int failedJobCountValue() const;
  [[nodiscard]] int completedJobCountValue() const;
  void updateSummaryStatus();

  QVector<PersistentJobRecord> jobs_;
  QString status_;
  QFutureWatcher<JobHistoryReadResult> history_watcher_;
  std::function<JobHistoryReadResult(bool)> history_reader_;
  bool history_loading_ = false;
  bool history_pending_ = false;
  bool recovery_pending_ = false;
  QString history_error_;
  QString startup_recovery_error_;
};

}  // namespace photo_editor::desktop
