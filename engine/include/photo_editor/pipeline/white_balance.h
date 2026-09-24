#pragma once

#include <array>

namespace photo_editor::pipeline {

[[nodiscard]] std::array<double, 3> preview_white_balance_multipliers(
    double temperature,
    double tint);

}  // namespace photo_editor::pipeline
