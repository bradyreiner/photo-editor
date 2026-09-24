#include "histogram_scope_coordinator.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSemaphore>
#include <QTimer>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::HistogramScopeRequest request_for(
    int generation,
    const QString& reason) {
  QImage image(4, 3, QImage::Format_ARGB32);
  image.fill(QColor(generation, 20, 30));
  return photo_editor::desktop::HistogramScopeRequest{
      .image = image,
      .generation = generation,
      .raw_clip_sample_count = generation + 10,
      .raw_shadow_clip_count = generation + 20,
      .raw_highlight_clip_count = generation + 30,
      .full_resolution = generation == 3,
      .render_reason = reason,
      .interaction_mode = QStringLiteral("slider"),
  };
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace photo_editor::desktop;

  QSemaphore first_compute_started;
  QSemaphore release_first_compute;
  std::atomic_int compute_count{0};
  QEventLoop completion_loop;
  QList<HistogramComputationResult> completed;
  auto completion_gap_coalesced = false;
  std::unique_ptr<HistogramScopeCoordinator> coordinator;
  coordinator = std::make_unique<HistogramScopeCoordinator>(
      &app,
      [&]() {
        if (completed.isEmpty()) {
          completion_gap_coalesced =
              coordinator->submit(
                  request_for(4, QStringLiteral("completion-gap"))) ==
              HistogramScopeSubmitDisposition::Coalesced;
        }
        completed.append(coordinator->result());
        if (!coordinator->complete()) {
          completion_loop.quit();
        }
      },
      [&](const QImage& image) {
        const auto call = compute_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (call == 1) {
          first_compute_started.release();
          release_first_compute.acquire();
        }
        HistogramComputationResult result;
        result.sample_count = image.width() * image.height();
        return result;
      });

  if (coordinator->submit(request_for(1, QStringLiteral("first"))) !=
          HistogramScopeSubmitDisposition::Started ||
      !coordinator->active() || !coordinator->workerRunning() ||
      coordinator->pending()) {
    return fail("first histogram request must start and publish active lifecycle state");
  }
  if (!first_compute_started.tryAcquire(1, 2000)) {
    return fail("first histogram request did not enter the worker");
  }

  if (coordinator->submit(request_for(2, QStringLiteral("obsolete"))) !=
          HistogramScopeSubmitDisposition::Coalesced ||
      coordinator->submit(request_for(3, QStringLiteral("latest"))) !=
          HistogramScopeSubmitDisposition::Coalesced ||
      !coordinator->pending()) {
    return fail("running histogram work must coalesce to one latest pending snapshot");
  }

  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &completion_loop, &QEventLoop::quit);
  timeout.start(5000);
  release_first_compute.release();
  completion_loop.exec();

  if (timeout.remainingTime() <= 0 || !completion_gap_coalesced ||
      completed.size() != 2 ||
      compute_count.load(std::memory_order_relaxed) != 2 ||
      completed[0].generation != 1 || completed[1].generation != 4 ||
      completed[1].render_reason != QStringLiteral("completion-gap") ||
      completed[1].interaction_mode != QStringLiteral("slider") ||
      completed[1].raw_clip_sample_count != 14 ||
      completed[1].raw_shadow_clip_count != 24 ||
      completed[1].raw_highlight_clip_count != 34 ||
      completed[1].full_resolution || completed[1].sample_count != 12 ||
      coordinator->active() || coordinator->pending() ||
      coordinator->workerRunning()) {
    return fail("coordinator must finish current work, run only the latest request, and become idle");
  }

  return EXIT_SUCCESS;
}
