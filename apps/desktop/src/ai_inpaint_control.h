#pragma once

#include <atomic>
#include <functional>
#include <memory>
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
    if (generation_) {
      generation_->store(generation_value_ + 1, std::memory_order_relaxed);
    }
    if (interrupt_) {
      interrupt_();
    }
  }
  void bindGenerationCancellation(
      std::shared_ptr<std::atomic<int>> generation, int current_value) {
    std::lock_guard lock(mutex_);
    generation_ = std::move(generation);
    generation_value_ = current_value;
    if (cancelled_.load() && generation_) {
      generation_->store(generation_value_ + 1, std::memory_order_relaxed);
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
  std::shared_ptr<std::atomic<int>> generation_;
  int generation_value_ = 0;
};

}  // namespace photo_editor::desktop
