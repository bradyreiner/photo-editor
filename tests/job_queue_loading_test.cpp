#include "job_queue_model.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QPersistentModelIndex>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>
#include <iostream>

namespace photo_editor::desktop {
struct JobQueueModelTestAccess {
  static void setReader(JobQueueModel& model, std::function<JobHistoryReadResult(bool)> reader) {
    model.history_reader_ = std::move(reader);
  }
};
}

bool wait_for(const std::function<bool()>& predicate, int timeout_ms = 10000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return predicate();
}

photo_editor::desktop::JobHistoryReadResult record_result(const QString& id) {
  photo_editor::desktop::PersistentJobRecord record;
  record.job_id = id;
  record.status = "Completed";
  return {{record}, {}};
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/job_history_loading_proof");
  photo_editor::desktop::JobQueueModel model;
  using namespace photo_editor::desktop;
  int heartbeat_ticks = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeat_ticks; });
  heartbeat.start(10);
  QElapsedTimer elapsed;
  elapsed.start();
  model.refresh();
  const auto request_ms = elapsed.elapsed();
  std::cout << "job history refresh returned in " << request_ms << " ms\n";
  if (request_ms >= 100) {
    std::cerr << "Job history refresh must not wait for the catalog on the UI thread\n";
    return 1;
  }
  if (!wait_for([&] { return !model.loading(); }) || model.historyError().isEmpty() || heartbeat_ticks < 20) {
    std::cerr << "Catalog outage must complete with an error while UI timers keep running; loading="
              << model.loading() << "; ticks=" << heartbeat_ticks
              << "; error=" << model.historyError().toStdString() << '\n'; return 1;
  }
  std::cout << "outage heartbeat ticks: " << heartbeat_ticks << '\n';
  JobQueueModelTestAccess::setReader(model, [](bool) { return record_result("saved"); });
  model.refresh();
  if (!wait_for([&] { return !model.loading(); }) || model.jobCount() != 1 || !model.historyError().isEmpty()) return 1;
  const QPersistentModelIndex saved_index(model.index(0, 0));
  int resets = 0;
  bool completed_update = false;
  QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
  QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] {
    if (saved_index.isValid() && saved_index.data(JobQueueModel::StatusRole) == "Completed")
      completed_update = true;
  });
  const auto publish_fixture = [&](const JobHistoryReadResult& result) {
    JobQueueModelTestAccess::setReader(model, [result](bool) { return result; });
    model.refresh();
    return wait_for([&] { return !model.loading(); });
  };
  auto running = record_result("saved");
  running.records[0].status = "Running";
  if (!publish_fixture(running)) return 1;
  auto inserted = record_result("new");
  inserted.records.push_back(running.records[0]);
  if (!publish_fixture(inserted) || !saved_index.isValid() || saved_index.row() != 1) return 1;
  std::swap(inserted.records[0], inserted.records[1]);
  if (!publish_fixture(inserted) || saved_index.row() != 0) return 1;
  if (!publish_fixture(record_result("saved")) || !saved_index.isValid() || resets != 0 ||
      !completed_update || saved_index.data(JobQueueModel::JobIdRole) != "saved") {
    std::cerr << "Queue refresh must preserve row identity through insertion, move, removal and completion\n";
    return 1;
  }
  struct ReaderState {
    QSemaphore release;
    std::atomic<int> calls{0};
    std::atomic<bool> finished{false};
  };
  auto state = std::make_shared<ReaderState>();
  JobQueueModelTestAccess::setReader(model, [state](bool) {
    if (++state->calls == 1) { state->release.acquire(); return record_result("obsolete"); }
    return record_result("newest");
  });
  bool obsolete_published = false;
  QObject::connect(&model, &JobQueueModel::jobsChanged, [&] {
    if (model.jobCount() != 0 && model.data(model.index(0, 0), JobQueueModel::JobIdRole) == "obsolete")
      obsolete_published = true;
  });
  model.refresh();
  if (!wait_for([&] { return state->calls.load() == 1; })) return 1;
  for (int i = 0; i < 100; ++i) model.refresh();
  if (model.jobCount() != 1) return 1;
  state->release.release();
  if (!wait_for([&] { return !model.loading(); }) || state->calls != 2 || obsolete_published ||
      model.data(model.index(0, 0), JobQueueModel::JobIdRole) != "newest") {
    std::cerr << "Refresh bursts must coalesce and never publish an obsolete snapshot\n"; return 1;
  }
  JobQueueModelTestAccess::setReader(model, [](bool) { return JobHistoryReadResult{{}, "offline"}; });
  model.refresh();
  if (!wait_for([&] { return !model.loading(); }) || model.jobCount() != 1 || model.historyError() != "offline") return 1;
  JobQueueModelTestAccess::setReader(model, [](bool) { return JobHistoryReadResult{}; });
  model.refresh();
  if (!wait_for([&] { return !model.loading(); }) || model.jobCount() != 0 || !model.historyError().isEmpty()) return 1;

  auto closing = std::make_unique<JobQueueModel>();
  auto closing_state = std::make_shared<ReaderState>();
  JobQueueModelTestAccess::setReader(*closing, [closing_state](bool recovery) {
    ++closing_state->calls;
    closing_state->release.acquire();
    closing_state->finished = recovery;
    return record_result("closed");
  });
  closing->initializeFromStore();
  if (!wait_for([&] { return closing_state->calls.load() == 1; })) return 1;
  elapsed.restart();
  closing.reset();
  const auto destruction_ms = elapsed.elapsed();
  closing_state->release.release();
  if (destruction_ms >= 100 || !wait_for([&] { return closing_state->finished.load(); })) {
    std::cerr << "Model destruction must not wait for or invalidate the detached reader\n"; return 1;
  }
  std::cout << "coalescing, stale-result rejection, history preservation, retry and close passed\n";
  return 0;
}
