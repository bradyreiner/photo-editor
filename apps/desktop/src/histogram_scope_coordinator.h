#pragma once

#include <functional>
#include <optional>
#include <utility>

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QtConcurrent>

#include "histogram_scope_service.h"

namespace photo_editor::desktop {

struct HistogramScopeRequest final {
  QImage image;
  int generation = 0;
  int raw_clip_sample_count = 0;
  int raw_shadow_clip_count = 0;
  int raw_highlight_clip_count = 0;
  bool full_resolution = false;
  QString render_reason;
  QString interaction_mode;
};

enum class HistogramScopeSubmitDisposition {
  Started,
  Coalesced,
};

class HistogramScopeCoordinator final {
 public:
  using CompletionCallback = std::function<void()>;
  using ComputeCallback =
      std::function<HistogramComputationResult(const QImage& image)>;

  HistogramScopeCoordinator(
      QObject* context,
      CompletionCallback completion,
      ComputeCallback compute = compute_histogram_scope_data)
      : compute_(std::move(compute)) {
    QObject::connect(
        &watcher_,
        &QFutureWatcher<HistogramComputationResult>::finished,
        context,
        [stage = std::move(completion)]() { stage(); });
  }

  [[nodiscard]] HistogramScopeSubmitDisposition submit(
      HistogramScopeRequest request) {
    // Keep the completed-but-not-yet-consumed result protected as active too;
    // otherwise a render callback in the same event-loop turn could replace
    // the watcher's future before the completion stage reads it.
    if (active_) {
      pending_request_ = std::move(request);
      return HistogramScopeSubmitDisposition::Coalesced;
    }
    active_ = true;
    start(std::move(request));
    return HistogramScopeSubmitDisposition::Started;
  }

  [[nodiscard]] HistogramComputationResult result() const {
    return watcher_.result();
  }

  // Completes the current result and immediately starts the latest coalesced
  // request, if present. Returns true while more work remains active.
  [[nodiscard]] bool complete() {
    if (pending_request_.has_value()) {
      auto request = std::move(*pending_request_);
      pending_request_.reset();
      start(std::move(request));
      return true;
    }
    active_ = false;
    return false;
  }

  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] bool pending() const { return pending_request_.has_value(); }
  [[nodiscard]] bool workerRunning() const { return watcher_.isRunning(); }

 private:
  void start(HistogramScopeRequest request) {
    watcher_.setFuture(QtConcurrent::run(
        [request = std::move(request), compute = compute_]() mutable {
          QElapsedTimer timer;
          timer.start();
          auto result = compute(request.image);
          result.generation = request.generation;
          result.full_resolution = request.full_resolution;
          result.raw_clip_sample_count = request.raw_clip_sample_count;
          result.raw_shadow_clip_count = request.raw_shadow_clip_count;
          result.raw_highlight_clip_count = request.raw_highlight_clip_count;
          result.render_reason = std::move(request.render_reason);
          result.interaction_mode = std::move(request.interaction_mode);
          result.elapsed_ms = timer.elapsed();
          return result;
        }));
  }

  ComputeCallback compute_;
  QFutureWatcher<HistogramComputationResult> watcher_;
  std::optional<HistogramScopeRequest> pending_request_;
  bool active_ = false;
};

}  // namespace photo_editor::desktop
