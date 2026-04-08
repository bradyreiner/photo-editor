#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

class JobRunner final : public QObject {
  Q_OBJECT

 public:
  explicit JobRunner(QObject* parent = nullptr);

 Q_INVOKABLE void wake();

 private:
  void handleAsyncJobFinished();
  void processNextJob();
  void runExportJob(const QString& job_id);
  void runImportJob(const QString& job_id);
  void runTimelineRenderJob(const QString& job_id);

  QFutureWatcher<QVariantMap> async_job_watcher_;
  QString active_job_id_;
  QString active_job_kind_;
  QElapsedTimer active_job_timer_;
};

}  // namespace photo_editor::desktop
