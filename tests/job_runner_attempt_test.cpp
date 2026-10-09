#include "app_bootstrap.h"
#include "job_runner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>
#include <iostream>

namespace photo_editor::desktop {
struct JobRunnerTestAccess {
  static auto begin(JobRunner& runner, const QString& id, const QString& kind) {
    PersistentJobRecord job;
    job.job_id = id;
    job.job_kind = kind;
    return runner.beginAttempt(job);
  }
  static bool refresh(JobRunner& runner, const JobRunner::AttemptPtr& attempt, const PersistentJobRecord& job) {
    return runner.refreshAttemptResources(attempt, job);
  }
  static void observe(JobRunner& runner) { runner.observeActiveJobCancellation(); }
  static bool reading(const JobRunner& runner) { return runner.cancellation_reading_; }
  static bool contains(const JobRunner& runner, const QString& id) {
    return runner.active_attempts_.contains(id);
  }
  static void forget(JobRunner& runner, const QString& id) { runner.active_attempts_.remove(id); }
  static void complete(JobRunner& runner, const JobRunner::AttemptPtr& attempt) {
    runner.handleAsyncJobFinished(attempt);
  }
  static void release(JobRunner& runner, const JobRunner::AttemptPtr& attempt) {
    runner.releaseAttempt(attempt);
  }
};
}  // namespace photo_editor::desktop
using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
  return 77;
#else
  QTemporaryDir runtime;
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  const auto helper = runtime.filePath("rust/target/debug/catalog_export_jobs");
  if (!runtime.isValid() || !QDir().mkpath(runtime.filePath("rust/target/debug")) ||
      !write(runtime.filePath("rust/Cargo.toml"), "") ||
      !write(helper, R"SH(#!/bin/sh
if [ "$1" = "upsert" ]; then
  printf '%s\n' "$2" >> "$PHOTO_EDITOR_APP_DATA_DIR/updates"
  exit 0
fi
response=$(cat "$PHOTO_EDITOR_APP_DATA_DIR/response")
printf '%s\n' "$1" >> "$PHOTO_EDITOR_APP_DATA_DIR/calls"
sleep 0.05
printf '%s\n' "$response"
)SH") || !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/attempt_proof");
  if (AppBootstrap::helperProgram("catalog_export_jobs") != helper) return 1;
  const auto fixture = [&](const QByteArray& json) { return write(runtime.filePath("response"), json); };
  const auto calls = [&] {
    QFile file(runtime.filePath("calls"));
    return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
  };
  const auto wait = [&](auto predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 3000) {
      app.processEvents();
      QThread::msleep(1);
    }
    return predicate();
  };
  const auto fail = [](const char* message) { std::cerr << message << '\n'; return 1; };
  JobRunner runner;
  // Admission remains closed in this execution-ownership test. Synthetic futures
  // exercise the actual completion/cancellation code independently of lane policy.
  auto first = JobRunnerTestAccess::begin(runner, "first", "timeline_render");
  auto second = JobRunnerTestAccess::begin(runner, "second", "catalog_import");
  if (!first || !second || first->watcher == second->watcher ||
      first->cancellation == second->cancellation ||
      JobRunnerTestAccess::begin(runner, "first", "asset_export")) return fail("attempt ownership or duplicate guard");
  PersistentJobRecord reserved;
  reserved.job_id = "first"; reserved.job_kind = "timeline_render";
  first->resources = inspect_job_resources(reserved);
  auto changed = reserved; changed.output_path = runtime.filePath("changed-output.mp4");
  if (JobRunnerTestAccess::refresh(runner, first, changed) ||
      JobRunnerTestAccess::contains(runner, "first") || !JobRunnerTestAccess::contains(runner, "second"))
    return fail("changed execution inputs must release only their own reservation");
  first = JobRunnerTestAccess::begin(runner, "first", "timeline_render");
  if (!fixture(R"([{"job_id":"first","status":"Running"},{"job_id":"second","status":"Canceling"}])")) return 1;
  const auto before_read = calls();
  JobRunnerTestAccess::observe(runner);
  if (!wait([&] { return !JobRunnerTestAccess::reading(runner); }) ||
      first->cancellation->load() || !second->cancellation->load() || calls() != before_read + 1)
    return fail("one read must apply cancellation independently to both attempts");

  // A replacement using the same durable ID is immune to the old batch result.
  if (!fixture(R"([{"job_id":"first","status":"Pausing"},{"job_id":"second","status":"Running"}])")) return 1;
  const auto before_stale = calls();
  JobRunnerTestAccess::observe(runner);
  if (!wait([&] { return calls() > before_stale; })) return 1;
  auto old_first = first;
  JobRunnerTestAccess::forget(runner, "first");
  first = JobRunnerTestAccess::begin(runner, "first", "timeline_render");
  if (!wait([&] { return !JobRunnerTestAccess::reading(runner); }) ||
      first->cancellation->load() || old_first->cancellation->load() || !second->cancellation->load())
    return fail("stale reply must not cross attempt identities; cancellation is sticky");
  JobRunnerTestAccess::complete(runner, old_first);
  JobRunnerTestAccess::release(runner, old_first);
  if (!JobRunnerTestAccess::contains(runner, "first")) return fail("old completion released replacement");
  QPointer<QFutureWatcher<QVariantMap>> old_watcher = old_first->watcher;
  old_first.reset();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  if (old_watcher) return fail("retired watcher was retained");

  if (!fixture(R"([{"job_id":"first","job_kind":"timeline_render","status":"Running"},{"job_id":"second","job_kind":"catalog_import","status":"Running"}])")) return 1;
  auto first_gate = std::make_shared<QSemaphore>();
  auto second_gate = std::make_shared<QSemaphore>();
  first->watcher->setFuture(QtConcurrent::run([first_gate] {
    first_gate->acquire(); return QVariantMap{{QStringLiteral("marker"), QStringLiteral("first")},
        {QStringLiteral("success"), false}, {QStringLiteral("errorMessage"), QStringLiteral("first-result")}};
  }));
  second->watcher->setFuture(QtConcurrent::run([second_gate] {
    second_gate->acquire(); return QVariantMap{{QStringLiteral("marker"), QStringLiteral("second")},
        {QStringLiteral("success"), false}, {QStringLiteral("errorMessage"), QStringLiteral("second-result")}};
  }));
  // Ensure a failed assertion cannot leave the test pool blocked forever.
  const auto unblock = qScopeGuard([&] { first_gate->release(); second_gate->release(); });
  QPointer<QFutureWatcher<QVariantMap>> second_watcher = second->watcher;
  std::weak_ptr weak_second = second;
  second.reset();
  second_gate->release();
  if (!wait([&] { return !JobRunnerTestAccess::contains(runner, "second"); }) ||
      !JobRunnerTestAccess::contains(runner, "first")) return fail("out-of-order completion released wrong attempt");
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  if (!weak_second.expired() || second_watcher) return fail("completion retained its attempt or watcher");
  first_gate->release();
  if (!wait([&] { return !JobRunnerTestAccess::contains(runner, "first"); })) return fail("first completion missing");
  if (first->watcher->result().value(QStringLiteral("marker")).toString() != QStringLiteral("first")) return fail("result routed to wrong watcher");
  first.reset();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  QFile updates_file(runtime.filePath("updates"));
  if (!updates_file.open(QIODevice::ReadOnly)) return fail("missing durable update requests");
  const auto updates = updates_file.readAll().trimmed().split('\n');
  if (updates.size() != 2) return fail("completion must write each job exactly once");
  const auto second_update = QJsonDocument::fromJson(updates[0]).object();
  const auto first_update = QJsonDocument::fromJson(updates[1]).object();
  if (second_update.value("job_id").toString() != "second" ||
      second_update.value("error_message").toString() != "second-result" ||
      first_update.value("job_id").toString() != "first" ||
      first_update.value("error_message").toString() != "first-result")
    return fail("completion routed results to wrong persistent records");

  auto closing = std::make_unique<JobRunner>();
  auto closing_attempt = JobRunnerTestAccess::begin(*closing, "closing", "asset_export");
  auto closing_gate = std::make_shared<QSemaphore>();
  closing_attempt->watcher->setFuture(QtConcurrent::run([closing_gate] {
    closing_gate->acquire(); return QVariantMap{};
  }));
  auto closing_watcher = closing_attempt->watcher;
  const auto before_close = calls();
  QElapsedTimer elapsed;
  elapsed.start();
  closing.reset();
  const auto close_ms = elapsed.elapsed();
  closing_gate->release();
  QThreadPool::globalInstance()->waitForDone();
  app.processEvents();
  if (close_ms >= 100 || closing_watcher || calls() != before_close)
    return fail("destruction must detach work without blocking or late store callbacks");
  closing_attempt.reset();
  std::cout << "Independent attempts, one cancellation read, sticky flags, same-ID stale replies, "
               "out-of-order completion, watcher reclamation and nonblocking destruction passed; close="
            << close_ms << " ms\n";
  return 0;
#endif
}
