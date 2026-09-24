#pragma once
#include <QVariantMap>
#include <algorithm>

namespace photo_editor::desktop {
inline QVariantMap gaussian_training_schedule(int steps, int registered_views, int max_splats = 1000000) {
  // Match the current native renderer admission limit. A larger training
  // artifact must not silently become unviewable in the photographic workspace.
  if (steps < 200 || steps > 100000 || registered_views < 8 || registered_views > 2000 ||
      max_splats < 100000 || max_splats > 2000000) return {};
  // Accumulate roughly a capture pass between refinements, bounded for very
  // small/large captures. Short diagnostic runs still need refinement cycles.
  const auto refine_every = std::min(std::clamp(registered_views, 32, 200), steps / 5);
  // Reserve a settling phase; never inherit a growth stop beyond total steps.
  const auto growth_stop = std::min(15000, steps * 4 / 5);
  return {{"contract", "capture-sized-refinement-v1"}, {"refineEvery", refine_every},
      {"growthStopIteration", growth_stop}, {"settlingSteps", steps - growth_stop},
      {"maxSplats", max_splats}, {"seed", 42}, {"evaluationSplitEvery", 8}};
}
}
