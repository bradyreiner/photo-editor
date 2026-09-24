#pragma once

#include <atomic>
#include <memory>

namespace photo_editor::desktop {

class PreviewSession final {
 public:
  PreviewSession()
      : latest_generation_(std::make_shared<std::atomic<int>>(0)) {}

  [[nodiscard]] int advanceGeneration() {
    ++generation_;
    latest_generation_->store(generation_, std::memory_order_relaxed);
    return generation_;
  }

  [[nodiscard]] int generation() const { return generation_; }

  [[nodiscard]] std::shared_ptr<std::atomic<int>> latestGeneration() const {
    return latest_generation_;
  }

  [[nodiscard]] bool isCurrent(int generation) const {
    return generation == generation_;
  }

  [[nodiscard]] bool canPublish(int generation, bool full_resolution) const {
    if (!isCurrent(generation)) {
      return false;
    }
    return generation >= (full_resolution ? last_full_generation_
                                          : last_interactive_generation_);
  }

  void markPublished(int generation, bool full_resolution) {
    if (!isCurrent(generation)) {
      return;
    }
    if (full_resolution) {
      last_full_generation_ = generation;
    } else {
      last_interactive_generation_ = generation;
    }
  }

  [[nodiscard]] int advancePublicationRevision() {
    return ++publication_revision_;
  }

  [[nodiscard]] int publicationRevision() const {
    return publication_revision_;
  }

  [[nodiscard]] int lastInteractiveGeneration() const {
    return last_interactive_generation_;
  }

  [[nodiscard]] int lastFullGeneration() const {
    return last_full_generation_;
  }

  PreviewSession(const PreviewSession&) = delete;
  PreviewSession& operator=(const PreviewSession&) = delete;

 private:
  int generation_ = 0;
  int last_interactive_generation_ = 0;
  int last_full_generation_ = 0;
  int publication_revision_ = 0;
  std::shared_ptr<std::atomic<int>> latest_generation_;
};

}  // namespace photo_editor::desktop
