#include "export_job_controller.h"
#include "app_bootstrap.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <iostream>

namespace photo_editor::desktop {
struct ExportJobControllerTestAccess {
  static void setReader(ExportJobController& controller, std::function<ExportHistoryReadResult()> reader) { controller.history_reader_ = std::move(reader); }
  static void reload(ExportJobController& controller) { controller.loadPersistedJobs(); }
};
}
using namespace photo_editor::desktop;
static bool wait_for(const std::function<bool()>& predicate, int timeout_ms = 10000) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1);
  }
  return predicate();
}
static ExportHistoryReadResult record(const QString& id) {
  PersistentJobRecord value; value.job_id = id; value.job_kind = "asset_export"; value.status = "Completed";
  return {{value}, {}};
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir runtime;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/export_history_loading_proof");
  photo_editor::desktop::ExportJobController controller;
  int ticks = 0;
  QTimer heartbeat; heartbeat.setInterval(10);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; }); heartbeat.start();
  QElapsedTimer timer; timer.start();
  controller.initializeFromStore();
  std::cout << "Export history initialization returned in " << timer.elapsed() << " ms\n";
  if (timer.elapsed() >= 100 || !controller.loading()) return 1;
  if (!wait_for([&] { return !controller.loading(); }) || controller.historyError().isEmpty() || ticks < 20) {
    std::cerr << "Outage check: loading=" << controller.loading() << "; ticks=" << ticks
              << "; error=" << controller.historyError().toStdString() << '\n';
    return 1;
  }
  std::cout << "Catalog outage heartbeat ticks: " << ticks << '\n';
  ExportJobControllerTestAccess::setReader(controller, [] { return record("retained"); });
  controller.initializeFromStore();
  if (!wait_for([&] { return !controller.loading(); }) || controller.jobCount() != 1 || !controller.historyError().isEmpty()) return 1;
  struct ReaderState { QSemaphore release; std::atomic_int calls{0}; std::atomic_bool finished{false}; };
  const auto state = std::make_shared<ReaderState>();
  bool obsolete_published = false, wrong_thread = false;
  QObject::connect(&controller, &ExportJobController::jobsChanged, [&] {
    wrong_thread = wrong_thread || QThread::currentThread() != controller.thread();
    for (const auto& job : controller.jobs()) obsolete_published = obsolete_published || job.toMap().value("jobId") == "obsolete";
  });
  ExportJobControllerTestAccess::setReader(controller, [state] {
    if (++state->calls == 1) { state->release.acquire(); return record("obsolete"); }
    auto result = record("fresh"); result.records[0].status = "Running"; return result;
  });
  controller.initializeFromStore();
  if (!wait_for([&] { return state->calls.load() == 1; })) return 1;
  for (int i = 0; i < 100; ++i) jobStoreNotifier()->notifyJobsChanged();
  state->release.release();
  if (!wait_for([&] { return !controller.loading(); }) || state->calls != 2 || obsolete_published || wrong_thread ||
      controller.jobs()[0].toMap().value("jobId") != "fresh" || !controller.busy()) return 1;
  ExportJobControllerTestAccess::setReader(controller, [] { return ExportHistoryReadResult{{}, "Unavailable"}; });
  controller.initializeFromStore();
  if (!wait_for([&] { return !controller.loading(); }) || controller.jobCount() != 1 || controller.historyError() != "Unavailable" ||
      controller.jobs()[0].toMap().value("jobId") != "fresh" || !controller.busy()) return 1;
  ExportJobControllerTestAccess::setReader(controller, []() -> ExportHistoryReadResult { throw std::runtime_error("Reader failed"); });
  controller.initializeFromStore();
  if (!wait_for([&] { return !controller.loading(); }) || controller.jobCount() != 1 || controller.historyError() != "Reader failed") return 1;
  ExportJobControllerTestAccess::setReader(controller, [] { return ExportHistoryReadResult{}; });
  controller.initializeFromStore();
  if (!wait_for([&] { return !controller.loading(); }) || controller.jobCount() != 0 || !controller.historyError().isEmpty() || controller.busy()) return 1;
  // The synchronous post-mutation snapshot must supersede an older pending read.
  const auto mutation = std::make_shared<ReaderState>();
  ExportJobControllerTestAccess::setReader(controller, [mutation] {
    if (++mutation->calls == 1) { mutation->release.acquire(); return record("obsolete"); }
    return record("mutated");
  });
  controller.initializeFromStore();
  if (!wait_for([&] { return mutation->calls.load() == 1; })) return 1;
  ExportJobControllerTestAccess::reload(controller);
  if (controller.jobCount() != 1 || controller.jobs()[0].toMap().value("jobId") != "mutated") return 1;
  mutation->release.release();
  if (!wait_for([&] { return !controller.loading(); }) || mutation->calls != 3 || obsolete_published || wrong_thread) return 1;
  const auto closing_state = std::make_shared<ReaderState>();
  auto closing = std::make_unique<ExportJobController>();
  ExportJobControllerTestAccess::setReader(*closing, [closing_state] {
    ++closing_state->calls; closing_state->release.acquire(); closing_state->finished = true; return record("closed");
  });
  closing->initializeFromStore();
  if (!wait_for([&] { return closing_state->calls.load() == 1; })) return 1;
  timer.restart(); closing.reset(); const auto destruction_ms = timer.elapsed();
  closing_state->release.release();
  if (destruction_ms >= 100 || !wait_for([&] { return closing_state->finished.load(); })) return 1;
#ifndef Q_OS_WIN
  // A controlled helper accepts every command. A background reader must only
  // issue list, never upsert or clear the legacy file as a side effect.
  const auto fake_root = runtime.filePath("read-only-helper");
  const auto helper_path = QDir(fake_root).filePath("rust/target/debug/catalog_export_jobs");
  if (!QDir().mkpath(QDir(fake_root).filePath("rust/target/debug")) || !QDir().mkpath(runtime.filePath("jobs"))) return 1;
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  const QByteArray legacy("{\"jobs\":[{\"job_id\":\"legacy\",\"job_kind\":\"asset_export\",\"status\":\"Completed\"}]}");
  if (!write(QDir(fake_root).filePath("rust/Cargo.toml"), "") || !write(runtime.filePath("jobs/index.json"), legacy) ||
      !write(helper_path, "#!/bin/sh\nprintf '%s\\n' \"$1\" >> \"$PHOTO_EDITOR_APP_DATA_DIR/helper-calls\"\nprintf '[]\\n'\n") ||
      !QFile::setPermissions(helper_path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", fake_root.toUtf8());
  if (AppBootstrap::helperProgram("catalog_export_jobs") != helper_path) return 1;
  ExportJobController read_only;
  read_only.initializeFromStore();
  if (!wait_for([&] { return !read_only.loading(); }) || read_only.jobCount() != 1 || !read_only.historyError().isEmpty()) return 1;
  QFile calls(runtime.filePath("helper-calls")), preserved(runtime.filePath("jobs/index.json"));
  if (!calls.open(QIODevice::ReadOnly) || calls.readAll() != "list\n" ||
      !preserved.open(QIODevice::ReadOnly) || preserved.readAll() != legacy) return 1;
  std::cout << "Background history issued list only and preserved legacy bytes\n";
#endif
  std::cout << "100 notifications coalesced; stale/error/exception/empty-retry/mutation/destruction checks passed\n";
  return 0;
}
