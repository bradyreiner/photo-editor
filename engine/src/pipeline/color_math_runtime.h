#pragma once

namespace photo_editor::pipeline {

[[nodiscard]] double smoothstep(double edge0, double edge1, double value);
[[nodiscard]] double bell_weight(double value, double center, double radius);
[[nodiscard]] double circular_weight(double value, double center, double radius);
[[nodiscard]] double circular_feathered_selection_weight(
    double value,
    double center,
    double radius,
    double softness);
[[nodiscard]] double wrap_unit(double value);
[[nodiscard]] double wrap_radians(double value);
[[nodiscard]] double shortest_angular_delta(double from, double to);

}  // namespace photo_editor::pipeline
