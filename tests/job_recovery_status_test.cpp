#include "job_queue_model.h"
#include "export_job_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

namespace photo_editor::desktop {
struct JobQueueModelTestAccess {
  static void setReader(JobQueueModel& model, const QString& error) {
    model.history_reader_ = [error](bool) {
      PersistentJobRecord record;
      record.job_id = "catalog-job";
      record.status = "Paused";
      return JobHistoryReadResult{{record}, error};
    };
  }
};
struct ExportJobControllerTestAccess {
  static void seed(ExportJobController& model) {
    ExportJobController::ExportJob job;
    job.job_id = "catalog-job";
    job.status = "Failed";
    model.jobs_.push_back(job);
  }
};
}
using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  const auto helper = runtime.filePath("rust/target/debug/catalog_export_jobs");
  if (!QDir().mkpath(runtime.filePath("rust/target/debug")) ||
      !write(runtime.filePath("rust/Cargo.toml"), "") || !write(helper, R"SH(#!/bin/sh
touch "$PHOTO_EDITOR_APP_DATA_DIR/helper-called"
printf '[{"job_id":"catalog-job","job_kind":"asset_export","status":"Paused"}]\n'
)SH") || !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/job_recovery_status_proof");
  JobQueueModel model;
  ExportJobController exports;
  ExportJobControllerTestAccess::seed(exports);
  int changed = 0;
  QObject::connect(&model, &JobQueueModel::startupRecoveryErrorChanged, [&] { ++changed; });
  const auto refresh = [&](const QString& error = {}) {
    JobQueueModelTestAccess::setReader(model, error);
    model.refresh();
    QElapsedTimer timer; timer.start();
    while (model.loading() && timer.elapsed() < 2000) { app.processEvents(); QThread::msleep(1); }
    return !model.loading();
  };
  const auto conflict = QStringLiteral("Local and catalog job versions conflict; both were preserved.");
  model.setStartupRecoveryError(conflict);
  model.setStartupRecoveryError(conflict);
  exports.setStartupRecoveryError(conflict);
  if (changed != 1 || !refresh() || model.jobCount() != 1 || !model.historyError().isEmpty() ||
      model.startupRecoveryError() != conflict || !model.status().contains(conflict) ||
      !model.statusForKind("asset_export").contains(conflict)) return 1;
  if (!refresh("history temporarily offline") || model.jobCount() != 1 ||
      model.historyError() != "history temporarily offline" || model.startupRecoveryError() != conflict) return 1;
  if (model.canResumeJob("catalog-job") || model.canDuplicateRenderJob("catalog-job") ||
      model.data(model.index(0), JobQueueModel::CanResumeRole).toBool()) {
    std::cerr << "Unresolved recovery still advertises writable job controls\n";
    return 1;
  }
  if (model.historyMutationsEnabled() || exports.historyMutationsEnabled() ||
      !exports.status().contains(conflict)) return 1;
  QElapsedTimer blocked;
  blocked.start();
  if (model.retryJob("catalog-job") || model.cancelJob("catalog-job") ||
      model.pauseJob("catalog-job") || model.resumeJob("catalog-job") ||
      model.setJobPriority("catalog-job", "high") ||
      model.setRenderResourcePolicy("catalog-job", "balanced") ||
      model.moveRenderJob("catalog-job", -1) || !model.duplicateRenderJob("catalog-job").isEmpty()) return 1;
  model.retryFailedJobs();
  model.removeJob("catalog-job");
  model.clearFinishedJobs();
  model.acknowledgeRecoveryReport();
  exports.retryJob("catalog-job");
  exports.removeJob("catalog-job");
  exports.clearFinishedJobs();
  if (QFile::exists(runtime.filePath("helper-called")) || blocked.elapsed() > 100 ||
      model.jobCount() != 1 || exports.jobCount() != 1) return 1;
  const auto center = model.jobsForCenter().first().toMap();
  for (const auto* key : {"canPause", "canResume", "canCancel", "canRetry",
                          "canChangePriority", "canDuplicate", "canMoveUp", "canMoveDown"})
    if (center.value(QString::fromLatin1(key)).toBool()) return 1;
  model.setStartupRecoveryError({});
  exports.setStartupRecoveryError({});
  if (changed != 2 || !refresh() || !model.startupRecoveryError().isEmpty() ||
      model.status().contains(conflict) || model.statusForKind("asset_export").contains(conflict)) return 1;
  if (!model.historyMutationsEnabled() || !exports.historyMutationsEnabled() ||
      !model.canResumeJob("catalog-job") || !model.canDuplicateRenderJob("catalog-job") ||
      !model.data(model.index(0), JobQueueModel::CanResumeRole).toBool() ||
      !model.resumeJob("catalog-job") || !QFile::exists(runtime.filePath("helper-called"))) return 1;
  std::cout << "Recovery error survives refresh; blocked controls make zero helper calls; explicit recovery restores controls\n";
  return 0;
}
