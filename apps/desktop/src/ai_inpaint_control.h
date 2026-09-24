#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <utility>

namespace photo_editor::desktop {

// The interrupt is owned only while an inference call is alive. Clearing it
// waits for any concurrent cancellation callback before its RunOptions dies.
class AiInpaintControl final {
 public:
  void cancel() {
    cancelled_.store(true);
    std::lock_guard lock(mutex_);
    if (interrupt_) {
      interrupt_();
    }
  }
  [[nodiscard]] bool cancelled() const { return cancelled_.load(); }
  void setInterrupt(std::function<void()> interrupt) {
    std::lock_guard lock(mutex_);
    interrupt_ = std::move(interrupt);
    if (cancelled_.load() && interrupt_) {
      interrupt_();
    }
  }

 private:
  std::atomic_bool cancelled_{false};
  std::mutex mutex_;
  std::function<void()> interrupt_;
};

}  // namespace photo_editor::desktop
