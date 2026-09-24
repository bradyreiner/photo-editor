#include "gaussian_training_schedule.h"
#include <iostream>
using photo_editor::desktop::gaussian_training_schedule;
int main() {
  const auto candidate = gaussian_training_schedule(3000, 32);
  if (candidate.value("refineEvery").toInt() != 32 || candidate.value("growthStopIteration").toInt() != 2400 ||
      candidate.value("settlingSteps").toInt() != 600 || candidate.value("maxSplats").toInt() != 1000000 ||
      candidate.value("seed").toInt() != 42 || candidate.value("evaluationSplitEvery").toInt() != 8) return 1;
  for (int steps = 200; steps <= 100000; steps += 200) for (const auto views : {8, 32, 199, 200, 201, 2000}) {
    const auto s = gaussian_training_schedule(steps, views);
    const auto refine = s.value("refineEvery").toInt(), stop = s.value("growthStopIteration").toInt();
    if (s.isEmpty() || refine < 32 || refine > 200 || stop <= refine || stop >= steps || stop > 15000 ||
        s.value("settlingSteps").toInt() != steps - stop) return 1;
  }
  if (gaussian_training_schedule(30000, 2000).value("refineEvery").toInt() != 200 ||
      gaussian_training_schedule(30000, 2000).value("growthStopIteration").toInt() != 15000) return 1;
  for (const auto steps : {-1, 0, 199, 100001}) if (!gaussian_training_schedule(steps, 32).isEmpty()) return 1;
  for (const auto views : {-1, 0, 7, 2001}) if (!gaussian_training_schedule(3000, views).isEmpty()) return 1;
  for (const auto budget : {100000, 500000, 1000000, 2000000}) {
    const auto schedule = gaussian_training_schedule(30000, 64, budget);
    if (schedule.value("maxSplats").toInt() != budget || schedule.value("seed").toInt() != 42 ||
        schedule.value("evaluationSplitEvery").toInt() != 8) return 1;
  }
  for (const auto budget : {-1, 0, 99999, 2000001})
    if (!gaussian_training_schedule(30000, 64, budget).isEmpty()) return 1;
  std::cout << "Capture-sized schedule: supported bounds, explicit seed/cap, refinement and settling phase pass.\n";
  return 0;
}
