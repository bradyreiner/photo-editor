#include "color_adjustment_scheduler.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using photo_editor::pipeline::run_color_adjustment_parallel_for;

  auto serial_sum = 0;
  run_color_adjustment_parallel_for(
      8, [&serial_sum](int index) { serial_sum += index; });
  if (serial_sum != 28) {
    return fail("serial scheduler work did not execute exactly once");
  }

  std::atomic<int> parallel_count{0};
  std::atomic<long long> parallel_sum{0};
  run_color_adjustment_parallel_for(
      4096,
      [&parallel_count, &parallel_sum](int index) {
        parallel_count.fetch_add(1, std::memory_order_relaxed);
        parallel_sum.fetch_add(index, std::memory_order_relaxed);
      });
  if (parallel_count.load(std::memory_order_relaxed) != 4096 ||
      parallel_sum.load(std::memory_order_relaxed) != 8386560LL) {
    return fail("parallel scheduler work was lost or duplicated");
  }

  auto exception_observed = false;
  try {
    run_color_adjustment_parallel_for(
        256,
        [](int index) {
          if (index == 1) {
            throw std::runtime_error("scheduler-test-error");
          }
        });
  } catch (const std::runtime_error& error) {
    exception_observed =
        std::string_view(error.what()) == "scheduler-test-error";
  }
  if (!exception_observed) {
    return fail("parallel scheduler did not propagate a worker exception");
  }

  return EXIT_SUCCESS;
}
