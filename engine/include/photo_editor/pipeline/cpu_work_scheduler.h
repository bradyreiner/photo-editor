#pragma once

#include <functional>

namespace photo_editor::pipeline {

// Synchronous, process-wide workers shared by CPU image stages. Parallel
// batches run on a fixed pool; callers wait without adding another worker.
// Small batches and calls nested within scheduled work execute inline. Work
// must not wait on another thread that needs this pool or a caller-held lock.
// All callbacks finish before return, including when a callback throws.
void run_cpu_parallel_for(int count, const std::function<void(int)>& work);

}  // namespace photo_editor::pipeline
