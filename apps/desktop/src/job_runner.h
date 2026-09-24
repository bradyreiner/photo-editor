#pragma once

#include "job_store.h"

#include <QElapsedTimer>
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
  void handleAsyncJobFinished();
  void processNextJob();
  void finishQueueObservation();
  void runExportJob(const QString& job_id);
  void runImportJob(const QString& job_id);
  void runTimelineRenderJob(const QString& job_id);

  QFutureWatcher<QVariantMap> async_job_watcher_;
  QFutureWatcher<JobCancellationReadResult> cancellation_watcher_;
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
  QString active_job_id_;
  QString active_job_kind_;
  qint64 active_queue_wait_ms_ = 0;
  QElapsedTimer active_job_timer_;
  std::shared_ptr<std::atomic_bool> active_job_cancellation_;
};

}  // namespace photo_editor::desktop
