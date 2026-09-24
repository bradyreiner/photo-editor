#pragma once

#include <functional>

namespace photo_editor::pipeline {

void run_color_adjustment_parallel_for(
    int count,
    const std::function<void(int)>& work);

}  // namespace photo_editor::pipeline
