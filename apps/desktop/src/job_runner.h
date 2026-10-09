#pragma once

#include "job_store.h"
#include "job_admission_policy.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QTimer>

#include <atomic>
#include <memory>

namespace photo_editor::desktop {

struct JobCancellationReadResult {
  QString job_id;
  std::shared_ptr<std::atomic_bool> cancellation;
  QString error;
  bool requested = false;
};

struct JobQueueReadResult {
  QVector<PersistentJobRecord> records;
  QHash<QString, JobResourceClaim> resources;
  QString error;
};

class JobRunner final : public QObject {
  Q_OBJECT

 public:
  explicit JobRunner(QObject* parent = nullptr);

  Q_INVOKABLE void initializeFromStore();
  Q_INVOKABLE void wake();

 signals:
  void startupRecoveryFinished(const QString& error);

 private:
  friend struct JobRunnerTestAccess;
  void observeActiveJobCancellation();
  void finishCancellationObservation();
  struct Attempt {
    QString job_id;
    QString job_kind;
    JobResourceClaim resources;
    qint64 queue_wait_ms = 0;
    QElapsedTimer timer;
    QPointer<QFutureWatcher<QVariantMap>> watcher;
    ~Attempt() { if (watcher) watcher->deleteLater(); }
    std::shared_ptr<std::atomic_bool> cancellation = std::make_shared<std::atomic_bool>(false);
  };
  using AttemptPtr = std::shared_ptr<Attempt>;
  JobAdmissionBudget admissionBudget() const;
  bool refreshAttemptResources(const AttemptPtr& attempt, const PersistentJobRecord& job);
  AttemptPtr beginAttempt(const PersistentJobRecord& job);
  bool isCurrentAttempt(const AttemptPtr& attempt) const;
  void releaseAttempt(const AttemptPtr& attempt);
  void handleAsyncJobFinished(const AttemptPtr& attempt);
  void processNextJob();
  void finishQueueObservation();
  void runExportJob(const AttemptPtr& attempt);
  void runImportJob(const AttemptPtr& attempt);
  void runTimelineRenderJob(const AttemptPtr& attempt);

  QFutureWatcher<QVector<JobCancellationReadResult>> cancellation_watcher_;
  QFutureWatcher<JobQueueReadResult> queue_watcher_;
  QFutureWatcher<QString> recovery_watcher_;
  bool recovery_requested_ = false;
  bool recovering_ = false;
  bool ready_ = false;
  bool queue_reading_ = false;
  bool queue_pending_ = false;
  bool cancellation_reading_ = false;
  bool cancellation_pending_ = false;
  QTimer wake_timer_;
  QHash<QString, AttemptPtr> active_attempts_;
};

}  // namespace photo_editor::desktop
