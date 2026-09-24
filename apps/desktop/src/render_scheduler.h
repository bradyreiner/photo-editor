#pragma once

#include <algorithm>
#include <functional>
#include <utility>

#include <QObject>
#include <QTimer>

namespace photo_editor::desktop {

class RenderScheduler final {
 public:
  using StageCallback = std::function<void()>;
  RenderScheduler(
      QObject* context,
      StageCallback interactive_stage,
      StageCallback full_stage,
      StageCallback pending_dispatch) {
    configureTimer(&interactive_timer_, context, std::move(interactive_stage));
    configureTimer(&full_timer_, context, std::move(full_stage));
    configureTimer(&pending_dispatch_timer_, context, std::move(pending_dispatch));
  }
  void scheduleInteractive(int delay_ms) { interactive_timer_.start(std::max(0, delay_ms)); }
  void scheduleFull(int delay_ms) { full_timer_.start(std::max(0, delay_ms)); }
  void schedulePendingDispatch() {
    if (!pending_dispatch_timer_.isActive()) {
      pending_dispatch_timer_.start(0);
    }
  }
  void schedulePendingDispatchIfIdle(bool pending, bool watcher_running) {
    if (pending && !watcher_running && !interactiveActive() && !fullActive()) {
      schedulePendingDispatch();
    }
  }
  void stopInteractive() { interactive_timer_.stop(); }
  void stopFull() { full_timer_.stop(); }
  void stopPendingDispatch() { pending_dispatch_timer_.stop(); }
  void stopAll() { stopInteractive(); stopFull(); stopPendingDispatch(); }
  [[nodiscard]] bool interactiveActive() const { return interactive_timer_.isActive(); }
  [[nodiscard]] bool fullActive() const { return full_timer_.isActive(); }
  [[nodiscard]] bool pendingDispatchActive() const { return pending_dispatch_timer_.isActive(); }
  [[nodiscard]] int interactiveDelayMs() const { return interactive_timer_.interval(); }
  [[nodiscard]] int fullDelayMs() const { return full_timer_.interval(); }
 private:
  static void configureTimer(QTimer* timer, QObject* context, StageCallback stage) {
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, context,
                     [callback = std::move(stage)]() { callback(); });
  }
  QTimer interactive_timer_;
  QTimer full_timer_;
  QTimer pending_dispatch_timer_;
};

}  // namespace photo_editor::desktop
