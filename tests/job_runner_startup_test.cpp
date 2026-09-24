#include "app_bootstrap.h"
#include "job_runner.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <iostream>

namespace photo_editor::desktop {
struct JobRunnerTestAccess {
  static bool ready(const JobRunner& runner) { return runner.ready_; }
  static bool recovering(const JobRunner& runner) { return runner.recovering_; }
};
}
using namespace photo_editor::desktop;

int real_catalog_proof(QCoreApplication& app, const QString& root) {
  // This mode is intentionally limited to a fresh test-owned directory.
  const QDir directory(root);
  if (!root.startsWith("/private/tmp/job-startup-real-") || !directory.exists() ||
      !directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", directory.filePath("runtime").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", directory.filePath("cache").toUtf8());
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  QString error;
  if (!AppBootstrap::ensureCatalogService(&error)) {
    std::cerr << error.toStdString() << '\n'; return 1;
  }
  const QMap<QString, QString> kinds{{"export", "asset_export"}, {"import", "catalog_import"},
                                    {"timeline", "timeline_render"}};
  for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
    PersistentJobRecord record;
    record.job_id = it.key(); record.job_kind = it.value();
    record.status = it.key() == "timeline" ? "Canceling" : it.key() == "import" ? "Pausing" : "Running";
    record.output_path = directory.filePath(it.key() + ".must-not-render.png");
    if (!upsertPersistedJobRecord(record, &error, true)) { std::cerr << error.toStdString() << '\n'; return 1; }
  }
  JobRunner runner;
  bool finished = false;
  QObject::connect(&runner, &JobRunner::startupRecoveryFinished, &app,
      [&](const QString& result) { error = result; finished = true; });
  int ticks = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start(10);
  QElapsedTimer timer; timer.start();
  runner.initializeFromStore();
  const auto admission = timer.elapsed();
  while (!finished && timer.elapsed() < 20000) { app.processEvents(); QThread::msleep(1); }
  const auto recovery_ms = timer.elapsed();
  if (!finished || !error.isEmpty() || admission >= 100 || ticks < 20) {
    std::cerr << "Real recovery failed: " << error.toStdString() << '\n'; return 1;
  }
  const auto records = loadPersistedJobRecords(&error, false);
  if (!error.isEmpty() || records.size() != 3) return 1;
  for (const auto& record : records) {
    const auto expected = record.job_id == "timeline" ? "Canceled" : "Paused";
    if (record.status != expected || !record.metadata_json.value("recoveredAfterRestart").toBool() ||
        record.metadata_json.value("requiresRecoveryConfirmation").toBool() != (record.status == "Paused") ||
        QFile::exists(record.output_path)) return 1;
    std::cout << "Persisted " << record.job_id.toStdString() << '=' << record.status.toStdString() << '\n';
  }
  std::cout << "Real PostgreSQL recovery admission=" << admission << " ms; recovery=" << recovery_ms
            << " ms; heartbeat ticks=" << ticks << "; no render files created\n";
  return 0;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() == 3 && app.arguments()[1] == "--real-catalog")
    return real_catalog_proof(app, app.arguments()[2]);
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
response=$(cat "$PHOTO_EDITOR_APP_DATA_DIR/response")
status=$(cat "$PHOTO_EDITOR_APP_DATA_DIR/status")
write_status=$(cat "$PHOTO_EDITOR_APP_DATA_DIR/write-status")
printf '%s\n' "$1" >> "$PHOTO_EDITOR_APP_DATA_DIR/calls"
sleep 0.25
if [ "$1" = upsert ]; then
  printf '%s\n' "$2" >> "$PHOTO_EDITOR_APP_DATA_DIR/writes"
  exit "$write_status"
fi
printf '%s\n' "$response"
exit "$status"
)SH") ||
      !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/startup_proof");
  if (AppBootstrap::helperProgram("catalog_export_jobs") != helper) return 1;
  const auto fixture = [&](const QByteArray& response, int status = 0, int write_status = 0) {
    return write(runtime.filePath("response"), response) &&
        write(runtime.filePath("status"), QByteArray::number(status)) &&
        write(runtime.filePath("write-status"), QByteArray::number(write_status));
  };
  const auto read = [&](const QString& name) {
    QFile file(runtime.filePath(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
  };
  const auto wait = [&](auto predicate) {
    QElapsedTimer deadline; deadline.start();
    while (!predicate() && deadline.elapsed() < 5000) { app.processEvents(); QThread::msleep(1); }
    return predicate();
  };
  const auto fail = [](const char* message) { std::cerr << message << '\n'; return 1; };
  const QByteArray interrupted = R"([
    {"job_id":"export","job_kind":"asset_export","status":"Running"},
    {"job_id":"import","job_kind":"catalog_import","status":"Pausing"},
    {"job_id":"timeline","job_kind":"timeline_render","status":"Canceling"},
    {"job_id":"done","job_kind":"asset_export","status":"Completed"}])";
  if (!fixture("[]")) return 1;
  auto runner = std::make_unique<JobRunner>();
  for (int i = 0; i < 100; ++i) runner->wake();
  app.processEvents();
  if (!read("calls").isEmpty()) return fail("notifications must not dispatch before initialization");
  int completions = 0;
  QString recovery_error;
  QObject::connect(runner.get(), &JobRunner::startupRecoveryFinished, &app,
      [&](const QString& error) { ++completions; recovery_error = error; });
  int ticks = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start(10);
  QElapsedTimer elapsed; elapsed.start();
  runner->initializeFromStore();
  const auto admission = elapsed.elapsed();
  for (int i = 0; i < 100; ++i) { runner->wake(); runner->initializeFromStore(); }
  while (elapsed.elapsed() < 300) { app.processEvents(); QThread::msleep(1); }
  const auto measured_ticks = ticks;
  std::cout << "Recovery admission=" << admission << " ms; heartbeat ticks=" << measured_ticks << '\n';
  if (admission >= 100 || measured_ticks < 20)
    return fail("slow recovery must keep the UI responsive");
  if (!wait([&] { return completions != 0; }) || completions != 1 ||
      !recovery_error.isEmpty() || !JobRunnerTestAccess::ready(*runner)) return fail("recovery must publish one successful completion");
  runner.reset();
  QThreadPool::globalInstance()->waitForDone();
  app.processEvents();
  if (!fixture(interrupted)) return 1;
  runner = std::make_unique<JobRunner>();
  completions = 0;
  QObject::connect(runner.get(), &JobRunner::startupRecoveryFinished, &app,
      [&](const QString& error) { ++completions; recovery_error = error; });
  runner->initializeFromStore();
  for (int i = 0; i < 100; ++i) { runner->wake(); runner->initializeFromStore(); }
  elapsed.restart();
  while (elapsed.elapsed() < 300) { app.processEvents(); QThread::msleep(1); }
  if (JobRunnerTestAccess::ready(*runner)) return fail("dispatch must remain closed until all recovery commits finish");
  if (!wait([&] { return completions == 1; }) || !recovery_error.isEmpty()) return 1;
  const auto writes = read("writes").trimmed().split('\n');
  if (writes.size() != 3) return fail("only interrupted records must be updated once");
  QMap<QString, QString> statuses;
  for (const auto& bytes : writes) {
    const auto record = QJsonDocument::fromJson(bytes).object();
    const auto metadata = record.value("metadata_json").toObject();
    if (!metadata.value("recoveredAfterRestart").toBool() ||
        metadata.value("recoveryAcknowledged").toBool()) return 1;
    statuses.insert(record.value("job_id").toString(), record.value("status").toString());
  }
  if (statuses != QMap<QString, QString>{{"export", "Paused"}, {"import", "Paused"},
                                        {"timeline", "Canceled"}}) return fail("recovery must preserve confirmation and cancellation semantics");
  runner->initializeFromStore();
  if (JobRunnerTestAccess::recovering(*runner)) return fail("ready runner must not recover current-session work again");
  runner.reset();
  QThreadPool::globalInstance()->waitForDone();
  app.processEvents();

  for (const auto& response : {QByteArray("unavailable"), QByteArray("{}"),
                              QByteArray("[null]"), interrupted}) {
    const bool write_failure = response == interrupted;
    if (!fixture(response, response == "unavailable" ? 1 : 0, write_failure ? 1 : 0)) return 1;
    runner = std::make_unique<JobRunner>();
    completions = 0;
    QObject::connect(runner.get(), &JobRunner::startupRecoveryFinished, &app,
        [&](const QString& error) { ++completions; recovery_error = error; });
    runner->initializeFromStore();
    if (!wait([&] { return completions == 1; }) || recovery_error.isEmpty() ||
        JobRunnerTestAccess::ready(*runner)) return fail("failed reads or commits must keep dispatch closed");
    if (QFile::exists(runtime.filePath("jobs/index.json"))) return fail("strict recovery must not fall back to a legacy commit");
    // Recovery retries itself without requiring another notification.
    if (!fixture("[]") || !wait([&] { return completions == 2; }) ||
        !recovery_error.isEmpty() || !JobRunnerTestAccess::ready(*runner)) return fail("recovery must retry before dispatch is enabled");
    runner.reset();
    QThreadPool::globalInstance()->waitForDone();
    app.processEvents();
  }
  if (!fixture("[]")) return 1;
  runner = std::make_unique<JobRunner>();
  bool published_after_destruction = false;
  QObject::connect(runner.get(), &JobRunner::startupRecoveryFinished, &app,
      [&](const QString&) { published_after_destruction = true; });
  const auto calls_before = read("calls").count('\n');
  runner->initializeFromStore();
  if (!wait([&] { return read("calls").count('\n') > calls_before; })) return 1;
  elapsed.restart(); runner.reset();
  const auto destruction = elapsed.elapsed();
  QThreadPool::globalInstance()->waitForDone();
  app.processEvents();
  if (destruction >= 100 || published_after_destruction) return fail("destruction must not block or publish a late recovery");
  std::cout << "Startup barrier, single recovery, durable pause/cancel, failure retry and destruction passed; destruction="
            << destruction << " ms\n";
  return 0;
#endif
}
