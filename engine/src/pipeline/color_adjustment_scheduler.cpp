#include "color_adjustment_scheduler.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

thread_local bool g_parallel_worker_active = false;

class ColorAdjustmentScheduler final {
 public:
  ColorAdjustmentScheduler() {
    const auto hardware_threads =
        std::max<unsigned int>(1, std::thread::hardware_concurrency());
    const auto worker_count = hardware_threads > 1 ? hardware_threads - 1 : 0;
    workers_.reserve(worker_count);
    for (auto worker_index = 0U; worker_index < worker_count; ++worker_index) {
      workers_.emplace_back([this]() { workerLoop(); });
    }
  }

  ColorAdjustmentScheduler(const ColorAdjustmentScheduler&) = delete;
  ColorAdjustmentScheduler& operator=(const ColorAdjustmentScheduler&) = delete;

  ~ColorAdjustmentScheduler() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    work_available_.notify_all();
    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  void run(int count, const std::function<void(int)>& work) {
    if (count <= 0) {
      return;
    }
    if (count < kSerialThreshold || workers_.empty() ||
        g_parallel_worker_active) {
      runSerial(count, work);
      return;
    }

    const auto item_count = static_cast<std::size_t>(count);
    const auto maximum_task_count =
        std::max<std::size_t>(1, workers_.size() + 1);
    const auto task_count = std::min<std::size_t>(
        maximum_task_count,
        std::max<std::size_t>(
            1,
            (item_count + kMinimumItemsPerTask - 1) /
                kMinimumItemsPerTask));
    if (task_count <= 1) {
      runSerial(count, work);
      return;
    }

    auto batch = std::make_shared<BatchState>();
    batch->remaining = task_count;

    {
      std::lock_guard lock(mutex_);
      for (std::size_t task_index = 1; task_index < task_count; ++task_index) {
        const auto begin = item_count * task_index / task_count;
        const auto end = item_count * (task_index + 1) / task_count;
        tasks_.push_back(Task{
            .begin = begin,
            .end = end,
            .work = work,
            .batch = batch,
        });
      }
    }
    work_available_.notify_all();

    try {
      const auto end = item_count / task_count;
      for (std::size_t index = 0; index < end; ++index) {
        work(static_cast<int>(index));
      }
    } catch (...) {
      std::lock_guard batch_lock(batch->mutex);
      if (batch->exception == nullptr) {
        batch->exception = std::current_exception();
      }
    }

    {
      std::lock_guard batch_lock(batch->mutex);
      --batch->remaining;
      if (batch->remaining == 0) {
        batch->completed.notify_one();
      }
    }

    std::unique_lock batch_lock(batch->mutex);
    batch->completed.wait(
        batch_lock, [&batch]() { return batch->remaining == 0; });
    if (batch->exception != nullptr) {
      std::rethrow_exception(batch->exception);
    }
  }

 private:
  static constexpr int kSerialThreshold = 16;
  static constexpr std::size_t kMinimumItemsPerTask = 8;

  struct BatchState final {
    std::mutex mutex;
    std::condition_variable completed;
    std::size_t remaining = 0;
    std::exception_ptr exception;
  };

  struct Task final {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::function<void(int)> work;
    std::shared_ptr<BatchState> batch;
  };

  static void runSerial(int count, const std::function<void(int)>& work) {
    for (int index = 0; index < count; ++index) {
      work(index);
    }
  }

  void workerLoop() {
    while (true) {
      Task task;
      {
        std::unique_lock lock(mutex_);
        work_available_.wait(
            lock, [this]() { return stopping_ || !tasks_.empty(); });
        if (stopping_ && tasks_.empty()) {
          return;
        }
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }

      try {
        g_parallel_worker_active = true;
        for (std::size_t index = task.begin; index < task.end; ++index) {
          task.work(static_cast<int>(index));
        }
      } catch (...) {
        std::lock_guard batch_lock(task.batch->mutex);
        if (task.batch->exception == nullptr) {
          task.batch->exception = std::current_exception();
        }
      }
      g_parallel_worker_active = false;

      {
        std::lock_guard batch_lock(task.batch->mutex);
        --task.batch->remaining;
        if (task.batch->remaining == 0) {
          task.batch->completed.notify_one();
        }
      }
    }
  }

  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable work_available_;
  std::deque<Task> tasks_;
  bool stopping_ = false;
};

ColorAdjustmentScheduler& color_adjustment_scheduler() {
  static ColorAdjustmentScheduler scheduler;
  return scheduler;
}

}  // namespace

void run_color_adjustment_parallel_for(
    int count,
    const std::function<void(int)>& work) {
  color_adjustment_scheduler().run(count, work);
}

}  // namespace photo_editor::pipeline
