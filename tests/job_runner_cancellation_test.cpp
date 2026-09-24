#include "app_bootstrap.h"
#include "job_runner.h"
#include "job_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <iostream>

namespace photo_editor::desktop {
struct JobRunnerTestAccess {
  static auto setActive(JobRunner& runner) {
    runner.ready_ = true;
    runner.active_job_id_ = QStringLiteral("active");
    runner.active_job_cancellation_ = std::make_shared<std::atomic_bool>(false);
    return runner.active_job_cancellation_;
  }
  static void observe(JobRunner& runner) { runner.observeActiveJobCancellation(); }
  static bool reading(const JobRunner& runner) { return runner.cancellation_reading_; }
  static QTimer* timer(JobRunner& runner) { return &runner.wake_timer_; }
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
delay=$(cat "$PHOTO_EDITOR_APP_DATA_DIR/delay")
printf '%s\n' "$1" >> "$PHOTO_EDITOR_APP_DATA_DIR/calls"
sleep "$delay"
printf '%s\n' "$response"
exit "$status"
)SH") ||
      !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/cancellation_proof");
  if (AppBootstrap::helperProgram("catalog_export_jobs") != helper) return 1;
  const auto fixture = [&](const QByteArray& response, int status = 0,
                           const QByteArray& delay = "0.1") {
    return write(runtime.filePath("response"), response) &&
        write(runtime.filePath("status"), QByteArray::number(status)) &&
        write(runtime.filePath("delay"), delay);
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
  JobRunner runner;
  if (!fixture("catalog unavailable", 1, "0.25")) return 1;
  const auto cancellation = JobRunnerTestAccess::setActive(runner);
  int heartbeats = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
  heartbeat.start(10);
  QElapsedTimer timer;
  timer.start();
  JobRunnerTestAccess::observe(runner);
  const auto elapsed = timer.elapsed();
  if (!wait([&] { return !JobRunnerTestAccess::reading(runner); })) return fail("read did not finish");
  std::cout << "Cancellation request returned in " << elapsed
            << " ms; catalog failure cancellation=" << cancellation->load()
            << "; heartbeat ticks=" << heartbeats << '\n';
  if (elapsed >= 100 || cancellation->load() || heartbeats < 15) return fail("outage must not block or cancel");
  if (!JobRunnerTestAccess::timer(runner)->isActive() ||
      JobRunnerTestAccess::timer(runner)->interval() != 1000) return fail("failed observation must retry");
  // Recovery needs no new notification: the already scheduled retry observes it.
  if (!fixture(R"([{"job_id":"active","status":"Pausing"}])") ||
      !wait([&] { return cancellation->load(); })) return fail("retry must observe recovered cancellation");

  const auto check = [&](const QByteArray& response, int status, bool expected) {
    JobRunnerTestAccess::timer(runner)->stop();
    if (!fixture(response, status)) return false;
    const auto flag = JobRunnerTestAccess::setActive(runner);
    JobRunnerTestAccess::observe(runner);
    if (!wait([&] { return !JobRunnerTestAccess::reading(runner); })) return false;
    JobRunnerTestAccess::timer(runner)->stop();
    return flag->load() == expected;
  };
  if (!check("not JSON", 0, false) || !check("{}", 0, false) ||
      !check("[null]", 0, false) || !check(R"([{"status":"Running"}])", 0, false) ||
      !check(R"([{"job_id":"other"},null])", 0, false)) {
    return fail("malformed history must not become proof of deletion");
  }
  if (!check("[]", 0, true) ||
      !check(R"([{"job_id":"other","status":"Running"}])", 0, true) ||
      !check(R"([{"job_id":"active","status":"Running"}])", 0, false) ||
      !check(R"([{"job_id":"active","status":"Canceling"}])", 0, true) ||
      !check(R"([{"job_id":"active","status":"Pausing"}])", 0, true) ||
      !check(R"([{"job_id":"active","status":"Running","metadata_json":{"cancellationRequested":true}}])", 0, true) ||
      !check(R"([{"job_id":"active","status":"Running","metadata_json":{"pauseRequested":true}}])", 0, true)) {
    return fail("authoritative running, removal, pause and cancel states must be distinguished");
  }
  // A legacy fallback is display/recovery data, not cancellation authority on error.
  const QByteArray legacy = R"({"jobs":[{"job_id":"active","status":"Pausing"}]})";
  if (!QDir().mkpath(runtime.filePath("jobs")) ||
      !write(runtime.filePath("jobs/index.json"), legacy) ||
      !check("catalog unavailable", 1, false) ||
      !check(R"([{"job_id":"active","status":"Running"}])", 0, false)) return fail("legacy fallback must not cancel on failure");
  QFile legacy_file(runtime.filePath("jobs/index.json"));
  if (!legacy_file.open(QIODevice::ReadOnly) || legacy_file.readAll() != legacy ||
      calls().contains("upsert")) return fail("cancellation observation must not migrate legacy history");
  legacy_file.close();

  // Superseding an attempt with the SAME ID must invalidate its pending read.
  if (!fixture(R"([{"job_id":"active","status":"Pausing"}])", 0, "0.25")) return 1;
  const auto old_flag = JobRunnerTestAccess::setActive(runner);
  const auto old_calls = calls().count('\n');
  JobRunnerTestAccess::observe(runner);
  if (!wait([&] { return calls().count('\n') > old_calls; })) return 1;
  const auto replacement = JobRunnerTestAccess::setActive(runner);
  if (!wait([&] { return !JobRunnerTestAccess::reading(runner); }) ||
      replacement->load() || old_flag->load()) return fail("late read must not cancel a replacement attempt");
  // A fresh observation for that replacement still has effect.
  JobRunnerTestAccess::observe(runner);
  if (!wait([&] { return !JobRunnerTestAccess::reading(runner); }) || !replacement->load()) return 1;

  // An in-flight read plus 100 requests produces only one follow-up. Cancellation
  // is sticky within an attempt; a later Running snapshot cannot undo the flag.
  if (!fixture(R"([{"job_id":"active","status":"Pausing"}])", 0, "0.25")) return 1;
  const auto burst_flag = JobRunnerTestAccess::setActive(runner);
  const auto before_burst = calls().count('\n');
  JobRunnerTestAccess::observe(runner);
  if (!wait([&] { return calls().count('\n') > before_burst; })) return 1;
  if (!fixture(R"([{"job_id":"active","status":"Running"}])")) return 1;
  for (int i = 0; i < 100; ++i) JobRunnerTestAccess::observe(runner);
  if (!wait([&] { return calls().count('\n') == before_burst + 2 &&
      !JobRunnerTestAccess::reading(runner); }) || !burst_flag->load()) return fail("coalescing must preserve cancellation");

  auto owned = std::make_unique<JobRunner>();
  const auto destroyed_flag = JobRunnerTestAccess::setActive(*owned);
  if (!fixture(R"([{"job_id":"active","status":"Pausing"}])", 0, "0.4")) return 1;
  const auto before_destroy = calls().count('\n');
  JobRunnerTestAccess::observe(*owned);
  if (!wait([&] { return calls().count('\n') > before_destroy; })) return 1;
  timer.restart();
  owned.reset();
  const auto destruction_ms = timer.elapsed();
  QThreadPool::globalInstance()->waitForDone();
  app.processEvents();
  if (destruction_ms >= 100 || destroyed_flag->load()) return fail("destroyed runner must not block or publish");
  std::cout << "Malformed/failing reads preserved active work; retry, pause/cancel/removal, read-only legacy, "
               "same-ID supersession, sticky cancellation and 101-to-2 read coalescing passed; destruction="
            << destruction_ms << " ms\n";
  return 0;
#endif
}
