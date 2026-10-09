#include "job_runner.h"
#include "job_store.h"
#include "app_bootstrap.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>

namespace photo_editor::desktop {
struct JobRunnerTestAccess {
  static void enable(JobRunner& runner) { runner.ready_ = true; }
  static QTimer* timer(JobRunner& runner) { return &runner.wake_timer_; }
  static bool reading(const JobRunner& runner) {
    return runner.cancellation_reading_ || runner.queue_reading_;
  }
  static auto setActive(JobRunner& runner) {
    PersistentJobRecord job;
    job.job_id = QStringLiteral("active");
    runner.active_attempts_.remove(job.job_id);
    return runner.beginAttempt(job)->cancellation;
  }
};
}
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
  return 77;
#else
  QTemporaryDir runtime;
  const auto root = runtime.path();
  const auto helper = runtime.filePath("rust/target/debug/catalog_export_jobs");
  if (!QDir().mkpath(runtime.filePath("rust/target/debug"))) return 1;
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  if (!write(runtime.filePath("rust/Cargo.toml"), "") ||
      !write(helper, "#!/bin/sh\nprintf '%s\\n' \"$1\" >> \"$PHOTO_EDITOR_APP_DATA_DIR/calls\"\nprintf '[{\"job_id\":\"active\",\"job_kind\":\"asset_export\",\"status\":\"Pausing\"}]\\n'\n") ||
      !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", root.toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/runner_wake_proof");
  if (AppBootstrap::helperProgram("catalog_export_jobs") != helper) return 1;
  JobRunner runner;
  JobRunnerTestAccess::enable(runner);
  int scans = 0;
  QObject::connect(JobRunnerTestAccess::timer(runner), &QTimer::timeout, [&] { ++scans; });
  const auto wait_for_scans = [&](int expected) {
    QElapsedTimer timer; timer.start();
    while ((scans < expected || JobRunnerTestAccess::reading(runner)) && timer.elapsed() < 2000) {
      app.processEvents(); QThread::msleep(1);
    }
    app.processEvents(); return scans == expected && !JobRunnerTestAccess::reading(runner);
  };
  for (int i = 0; i < 100; ++i) jobStoreNotifier()->notifyJobsChanged();
  if (!wait_for_scans(1)) return 1;
  const auto cancellation = JobRunnerTestAccess::setActive(runner);
  for (int i = 0; i < 100; ++i) jobStoreNotifier()->notifyJobsChanged();
  if (!wait_for_scans(2) || !cancellation->load()) return 1;
  // A fresh change expedites one deferred scan; further signals do not multiply it.
  JobRunnerTestAccess::timer(runner)->start(120);
  for (int i = 0; i < 100; ++i) runner.wake();
  if (JobRunnerTestAccess::timer(runner)->interval() != 0 || !wait_for_scans(3)) return 1;
  QFile calls(runtime.filePath("calls"));
  if (!calls.open(QIODevice::ReadOnly) || calls.readAll() != "list\nlist\nlist\n") return 1;
  std::cout << "300 wakeups produced 3 scans; active cancellation and deferred-scan expedite passed\n";
  return 0;
#endif
}
