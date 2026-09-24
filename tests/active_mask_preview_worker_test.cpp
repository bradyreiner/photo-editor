#include "active_mask_preview_worker.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <iostream>

using namespace photo_editor::desktop;

template <typename Predicate> bool wait_until(Predicate predicate) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
  }
  return predicate();
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto ui_thread = QThread::currentThreadId();
  QStringList published;
  QSemaphore entered;
  QSemaphore release;
  bool delivery_on_ui = true;
  ActiveMaskPreviewWorker worker;
  worker.configure([&](ActiveMaskPreviewRequest request) {
    if (request.key == "first" || request.key == "unwanted") {
      entered.release();
      (void)release.tryAcquire(1, 5000);
    }
    return ActiveMaskPreviewResult{.request = request, .source = request.key,
        .worker_thread = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId())), .success = true};
  }, [&](const ActiveMaskPreviewResult& result) {
    delivery_on_ui = delivery_on_ui && QThread::currentThreadId() == ui_thread;
    published.append(result.source);
    return true;
  });
  worker.submit(ActiveMaskPreviewRequest{.key = "first"});
  if (!entered.tryAcquire(1, 1000)) return 1;
  for (int i = 0; i < 100; ++i) worker.submit(ActiveMaskPreviewRequest{.key = QString::number(i)});
  if (worker.started != 1 || !worker.pending()) return 1;
  release.release();
  if (!wait_until([&] { return !worker.running() && !worker.pending(); }) ||
      published != QStringList{"99"} || worker.started != 2 || worker.discarded != 1 || !delivery_on_ui ||
      worker.last_worker_thread == QString::number(reinterpret_cast<quintptr>(ui_thread))) return 1;
  // Going back to a cache hit should not launch a replacement or publish an
  // unwanted completed request, even when its renderer ignores cancellation.
  worker.submit(ActiveMaskPreviewRequest{.key = "unwanted"});
  if (!entered.tryAcquire(1, 1000)) return 1;
  worker.want("cached");
  release.release();
  if (!wait_until([&] { return !worker.running(); }) || published.size() != 1 || worker.pending()) return 1;

  QSemaphore destroy_entered;
  QSemaphore destroy_release;
  QSemaphore exited;
  int late_delivery = 0;
  auto closing = std::make_unique<ActiveMaskPreviewWorker>();
  closing->configure([&](ActiveMaskPreviewRequest request) {
    destroy_entered.release();
    (void)destroy_release.tryAcquire(1, 5000);
    exited.release();
    return ActiveMaskPreviewResult{.request = request, .source = "late", .success = true};
  }, [&](const auto&) { ++late_delivery; return true; });
  closing->submit(ActiveMaskPreviewRequest{.key = "closing"});
  if (!destroy_entered.tryAcquire(1, 1000)) return 1;
  QElapsedTimer destruction;
  destruction.start();
  closing.reset();
  const auto destruction_ms = destruction.elapsed();
  destroy_release.release();
  if (destruction_ms > 33 || !exited.tryAcquire(1, 1000)) return 1;
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  if (late_delivery != 0) return 1;
  // Complete-but-undelivered futures still occupy the admission slot.
  QSemaphore finished_render;
  ActiveMaskPreviewWorker completed;
  completed.configure([&](ActiveMaskPreviewRequest request) {
    finished_render.release();
    return ActiveMaskPreviewResult{.request = request, .source = request.key, .success = true};
  }, [](const auto&) { return true; });
  completed.submit(ActiveMaskPreviewRequest{.key = "a"});
  if (!finished_render.tryAcquire(1, 1000)) return 1;
  if (!QThreadPool::globalInstance()->waitForDone(1000)) return 1;
  completed.submit(ActiveMaskPreviewRequest{.key = "b"});
  if (completed.started != 1 || !completed.pending() ||
      !wait_until([&] { return !completed.running() && !completed.pending(); }) || completed.published != 1) return 1;
  ActiveMaskPreviewWorker failing;
  failing.configure([](ActiveMaskPreviewRequest request) {
    return ActiveMaskPreviewResult{.request = request};
  }, [&](const auto&) {
    failing.submit(ActiveMaskPreviewRequest{.key = "bad"});
    return false;
  });
  failing.submit(ActiveMaskPreviewRequest{.key = "bad"});
  if (!wait_until([&] { return !failing.running(); })) return 1;
  failing.submit(ActiveMaskPreviewRequest{.key = "bad"});
  if (failing.started != 1 || failing.pending()) return 1;
  failing.want("changed");
  failing.submit(ActiveMaskPreviewRequest{.key = "bad"});
  if (!wait_until([&] { return !failing.running(); }) || failing.started != 2) return 1;
  std::cout << "100 edits coalesced to newest; failure suppression, cache return, UI delivery, completion race and nonblocking destruction passed ("
            << destruction_ms << " ms).\n";
  return 0;
}
