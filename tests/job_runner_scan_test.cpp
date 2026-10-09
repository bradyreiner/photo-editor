#include "app_bootstrap.h"
#include "job_runner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <iostream>

namespace photo_editor::desktop {
struct JobRunnerTestAccess {
  static void enable(JobRunner& runner) { runner.ready_ = true; }
  static void scan(JobRunner& runner) { runner.processNextJob(); }
  static bool reading(const JobRunner& runner) { return runner.queue_reading_; }
  static void observeOnly(JobRunner& runner) {
    QObject::disconnect(&runner.queue_watcher_, &QFutureWatcher<JobQueueReadResult>::finished,
        &runner, &JobRunner::finishQueueObservation);
  }
  static bool observed(const JobRunner& runner) { return runner.queue_watcher_.isFinished(); }
  static auto observation(const JobRunner& runner) { return runner.queue_watcher_.result(); }
  static QTimer* timer(JobRunner& runner) { return &runner.wake_timer_; }
  static QString active(const JobRunner& runner) { return runner.active_attempts_.isEmpty() ? QString{} : runner.active_attempts_.cbegin().key(); }
};
}
using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
  return 77;
#else
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  const auto helper = runtime.filePath("rust/target/debug/catalog_export_jobs");
  if (!QDir().mkpath(runtime.filePath("rust/target/debug")) ||
      !write(runtime.filePath("rust/Cargo.toml"), "") ||
      !write(helper, R"SH(#!/bin/sh
response=$(cat "$PHOTO_EDITOR_APP_DATA_DIR/response")
status=$(cat "$PHOTO_EDITOR_APP_DATA_DIR/status")
printf '%s\n' "$1" >> "$PHOTO_EDITOR_APP_DATA_DIR/calls"
sleep 0.25
printf '%s\n' "$response"
exit "$status"
)SH") ||
      !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/scan_proof");
  if (AppBootstrap::helperProgram("catalog_export_jobs") != helper) return 1;
  const auto fixture = [&](const QByteArray& response, int status = 0) {
    return write(runtime.filePath("response"), response) &&
        write(runtime.filePath("status"), QByteArray::number(status));
  };
  const auto calls = [&] {
    QFile file(runtime.filePath("calls"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
  };
  const auto wait = [&](auto predicate) {
    QElapsedTimer deadline; deadline.start();
    while (!predicate() && deadline.elapsed() < 4000) {
      app.processEvents(); QThread::msleep(1);
    }
    return predicate();
  };
  const auto fail = [](const char* message) {
    std::cerr << message << '\n'; return 1;
  };
  if (!fixture("[]")) return 1;
  JobRunner runner;
  JobRunnerTestAccess::enable(runner);
  int heartbeats = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
  heartbeat.start(10);
  QElapsedTimer elapsed;
  elapsed.start();
  JobRunnerTestAccess::scan(runner);
  const auto admission_ms = elapsed.elapsed();
  while (elapsed.elapsed() < 300) {
    app.processEvents();
    QThread::msleep(1);
  }
  const auto measured_heartbeats = heartbeats;
  if (!wait([&] { return !JobRunnerTestAccess::reading(runner); })) return 1;
  std::cout << "Idle scan admission=" << admission_ms << " ms; heartbeat ticks in 300 ms="
            << measured_heartbeats << '\n';
  if (admission_ms >= 100 || measured_heartbeats < 20) return fail("idle reads must leave the UI responsive");

  // Fallback history is neither dispatch authority during an outage nor a
  // reason for an observer to write/migrate it in the background.
  const QByteArray legacy = R"({"jobs":[{"job_id":"legacy","job_kind":"catalog_import","status":"Queued"}]})";
  if (!QDir().mkpath(runtime.filePath("jobs")) ||
      !write(runtime.filePath("jobs/index.json"), legacy)) return 1;
  for (const auto& invalid : {QByteArray("unavailable"), QByteArray("{}"),
                             QByteArray("[null]"), QByteArray(R"([{"status":"Queued"}])")}) {
    if (!fixture(invalid, invalid == "unavailable" ? 1 : 0)) return 1;
    JobRunnerTestAccess::scan(runner);
    if (!wait([&] { return !JobRunnerTestAccess::reading(runner); }) ||
        !JobRunnerTestAccess::active(runner).isEmpty() ||
        !JobRunnerTestAccess::timer(runner)->isActive() ||
        JobRunnerTestAccess::timer(runner)->interval() != 1000) return fail("failed reads must not dispatch and must retry");
    JobRunnerTestAccess::timer(runner)->stop();
  }
  QFile legacy_file(runtime.filePath("jobs/index.json"));
  if (!legacy_file.open(QIODevice::ReadOnly) || legacy_file.readAll() != legacy ||
      calls().contains("upsert")) return fail("observation must not migrate fallback history");
  legacy_file.close();
  // Retire only this test-owned fixture; otherwise the legacy compatibility
  // path intentionally supplies records when an authoritative queue is empty.
  if (!QFile::remove(runtime.filePath("jobs/index.json"))) return 1;
  if (!fixture("unavailable", 1)) return 1;
  JobRunnerTestAccess::scan(runner);
  if (!wait([&] { return !JobRunnerTestAccess::reading(runner); })) return 1;
  const auto before_retry = calls().count('\n');
  if (!fixture("[]") || !wait([&] {
        return calls().count('\n') > before_retry && !JobRunnerTestAccess::reading(runner);
      }) || JobRunnerTestAccess::timer(runner)->isActive()) return fail("recovered queue must be observed without a new notification");

  // A queued snapshot superseded while its helper is running must never start
  // work. A burst requests one fresh read, not 100 more helper processes.
  if (!fixture(R"([{"job_id":"obsolete","job_kind":"catalog_import","status":"Queued"}])")) return 1;
  const auto before_burst = calls().count('\n');
  JobRunnerTestAccess::scan(runner);
  if (!wait([&] { return calls().count('\n') > before_burst; }) || !fixture("[]")) return 1;
  for (int i = 0; i < 100; ++i) JobRunnerTestAccess::scan(runner);
  if (!wait([&] { return calls().count('\n') == before_burst + 2 &&
      !JobRunnerTestAccess::reading(runner) && !JobRunnerTestAccess::timer(runner)->isActive(); }) ||
      !JobRunnerTestAccess::active(runner).isEmpty() || calls().contains("upsert")) return fail("superseded snapshot must not dispatch; burst must coalesce");

  QJsonArray pending;
  for (int i = 0; i < 100; ++i) pending.push_back(QJsonObject{
      {"job_id", QStringLiteral("candidate-%1").arg(i)}, {"job_kind", "asset_export"},
      {"status", "Queued"}, {"metadata_json", QJsonObject{{"queueOrder", 100-i}}}});
  if (!fixture(QJsonDocument(pending).toJson())) return 1;
  JobRunner bounded;
  JobRunnerTestAccess::enable(bounded);
  JobRunnerTestAccess::observeOnly(bounded);
  JobRunnerTestAccess::scan(bounded);
  if (!wait([&] { return JobRunnerTestAccess::observed(bounded); })) return 1;
  const auto observed = JobRunnerTestAccess::observation(bounded);
  if (observed.records.size() != 100 || observed.resources.size() != 1 ||
      !observed.resources.contains("candidate-99")) return fail("inspect only the first admissible candidate in priority order");
  if (!fixture("[]")) return 1;

  auto closing = std::make_unique<JobRunner>();
  JobRunnerTestAccess::enable(*closing);
  const auto before_destroy = calls().count('\n');
  JobRunnerTestAccess::scan(*closing);
  if (!wait([&] { return calls().count('\n') > before_destroy; })) return 1;
  elapsed.restart();
  closing.reset();
  const auto destruction_ms = elapsed.elapsed();
  QThreadPool::globalInstance()->waitForDone();
  app.processEvents();
  if (destruction_ms >= 100) return fail("runner destruction must not wait for the database");
  std::cout << "Read-only fallback, malformed/outage retry, stale queued snapshot, 101-to-2 coalescing "
               "and safe destruction passed; destruction=" << destruction_ms << " ms\n";
  return 0;
#endif
}
